#include "g1dsp.h"

#include "mc68k/hdi08.h"
#include "dsp56kEmu/jit.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <type_traits>
#include <vector>

namespace g1
{
	namespace
	{
		// The DSP56303 has 4K of internal P and 2K+2K of X/Y. Plenty is reserved in case some
		// G1 program uses more; the boot ROM lives at $FF0000.
		constexpr dsp56k::TWord g_pMemSize = 0x10000;
		constexpr dsp56k::TWord g_xyMemSize = 0x10000;
		constexpr dsp56k::TWord g_externalMemAddr = 0x8000;
		constexpr dsp56k::TWord g_bootRom = 0xff0000;

		// The G1 works at 96 kHz with the DSPs at 82.944 MHz: 864 cycles per sample.
		constexpr uint32_t g_samplerate = 96000;
		constexpr uint32_t g_dspClock = 82944000;
		constexpr uint32_t g_cyclesPerFrame = g_dspClock / g_samplerate;	// 864

		// The ESSIs run on the clock derived from their CRA: 2*(PM+1)*24 cycles per word, 96 on the
		// links between DSPs (PM=1: 9 words per sample, exactly what DMA4 sends each block) and 144
		// on DSP 3 towards the codec (PM=2), whose receiver takes the link's 96 all the same. The
		// core's ESSI clock ticks at one rate and serves each direction of a port every (divider + 1)
		// ticks, so the tick is the link's word: 96 cycles, 9 per sample. The codec's words into DSP 0
		// come every 432 cycles, two per sample, which is 4.5 ticks: see codecTick().
		constexpr uint32_t g_essiBaseCyclesPerWord = g_cyclesPerFrame / 2;
		constexpr uint32_t g_essiTickCycles = 96;
		constexpr uint32_t g_ticksPerSample = g_cyclesPerFrame / g_essiTickCycles;	// 9
		constexpr uint32_t g_codecRxTicks = 4;
		static_assert(g_codecRxTicks * 2 + 1 == g_ticksPerSample, "two codec words per sample, 4 and 5 ticks apart");

		// In asynchronous mode the receiver uses the transmitter's clock (external SC0), not its
		// own CRA. Every DSP transmitting down the chain has PM=1: 96 cycles per word. DSP 3 has
		// PM=2 because its CRA is set for the codec: its receiver would run at 144 and could only
		// take 6 of the 9 words of each sample.
		constexpr uint32_t g_linkCyclesPerWord = 96;

		// Link delay between DSPs, in blocks. The threads sync every ~4,000 cycles (about 5
		// blocks): the receiver takes the block from g_linkLatency blocks ago, which has surely
		// arrived. On the hardware it is less than a block; here it is ~83 us per DSP.
		constexpr uint64_t g_linkLatency = 8;
		// First instruction of the OS's idle loop (skipIdle).
		constexpr dsp56k::TWord g_idlePc = 0x16c;
		// Each ESSI's receive DMA writes into a 9-word ring: X:$6C0 (ESSI0) and X:$6C9 (ESSI1).
		// Word i of the block always goes to base+i.
		constexpr dsp56k::TWord g_linkBase[2] = {0x6c0, 0x6c9};
		// The divider that the core's ESSI clock takes as "never serve" (cmake/Dsp56300.cmake).
		constexpr uint32_t g_rxNeverServed = 0xffffffff;

		// Word period derived from a CRA (DSP56303UM, fig. 7-3), as in the core.
		uint32_t essiWordCycles(const dsp56k::TWord _cra)
		{
			static constexpr uint32_t bits[8] = {8, 12, 16, 24, 32, 32, 24, 24};
			const uint32_t pm = (_cra & 0xff) + 1;
			const uint32_t prescale = (_cra & (1u << 11)) ? 1 : 8;
			return 2 * pm * prescale * bits[(_cra >> 19) & 7];
		}

		// Each DSP's IRQD pin gets the sample clock; its vector ($16) jumps to the block routine
		// ($175 or later), which computes one sample of the patch.
		constexpr dsp56k::TWord g_irqdVector = 0x16;

		// Maximum number of cycles a DSP is allowed to run in one CPU wait.
		constexpr uint64_t g_waitClamp = 200000;
		// ...and in a status (ISR) poll, much less: a few words of margin.
		constexpr uint64_t g_isrWaitClamp = 2000;
	}

	Dsp::Dsp(mc68k::Hdi08& _hdiUc, const uint32_t _index)
		: m_hdiUc(_hdiUc)
		, m_index(_index)
		, m_memory(m_validator, g_pMemSize, g_xyMemSize, g_externalMemAddr)
		, m_dsp(m_memory, &m_periph, &m_periphNop)
	{
		auto config = m_dsp.getJit().getConfig();
		config.aguSupportBitreverse = true;
		config.linkJitBlocks = false;
		config.dynamicPeripheralAddressing = false;
		config.dynamicFastInterrupts = true;
		config.maxInstructionsPerBlock = 32;
		// G1_JITBLOCK=n: at most n instructions per JIT block (diagnostics: 1 compiles each one alone).
		if(const char* jb = std::getenv("G1_JITBLOCK"))
			config.maxInstructionsPerBlock = static_cast<uint32_t>(std::max(0, std::atoi(jb)));	// 0: no limit
		// No cache of single-instruction blocks. The JIT keeps a destroyed one-op block by its opcode
		// and reuses it at that address, without looking at whether the address has become a loop
		// end since. The OS moves the main loop's end (LA) with every patch, so a NOP compiled as
		// plain code came back at the new end: DSP 0 ran out of the main loop into the sample
		// routine, its register stack crept until an RTI looped for ever, and the OS stopped
		// answering the editor (NOTES.md, "A cached block at the loop end"). Costs ~1-2 % of speed.
		// G1_SINGLEOP_CACHE=1 turns it back on, only to compare.
		config.cacheSingleOpBlocks = std::getenv("G1_SINGLEOP_CACHE") != nullptr;
		config.maxDoIterations = 1;
		// Experiments with the JIT's limits (docs/core-upgrade-plan.md, step 4): G1_JIT_LINK=1 links
		// blocks, G1_DO_ITERS=n (a power of two, 0 = unbounded) runs n loop iterations per block.
		if(const char* v = std::getenv("G1_JIT_LINK"))
			config.linkJitBlocks = std::atoi(v) != 0;
		if(const char* v = std::getenv("G1_DO_ITERS"))
			config.maxDoIterations = static_cast<uint32_t>(std::max(0, std::atoi(v)));
		// The OS writes module code into program memory on every patch load, over code compiled for
		// the previous patch. Marking each such address volatile for ever made everything around it
		// run one instruction per block, slower with each load (#6). Destroying the written block is
		// enough: a block ends right after its program-memory write, and blocks are not linked.
		// G1_VOLATILE_P=1 brings the marks back, only to compare.
		config.trackVolatilePMemory = std::getenv("G1_VOLATILE_P") != nullptr;
		m_dsp.getJit().setConfig(config);

		// Program memory full of RTS: a jump into garbage does not compile odd things.
		for(dsp56k::TWord i = 0; i < m_memory.sizeP(); ++i)
		{
			m_memory.set(dsp56k::MemArea_P, i, 0x00000c);
			m_dsp.getJit().notifyProgramMemWrite(i);
		}

		auto& clock = m_periph.getEssiClock();
		clock.setClockSource(dsp56k::EsxiClock::ClockSource::Cycles);
		clock.setSamplerate(g_samplerate * 2);
		clock.setCyclesPerSample(g_essiTickCycles);

		// Link by position: when the receiver asks for a frame, the emulator looks at which ring
		// word its DMA will write next and gives it that channel from the previous DSP's block. So
		// channel i always ends up at base+i, like on the hardware, regardless of the phase between
		// the ESSIs or of when the receiver was re-enabled (the hardware gets that from its common clock).
		for(uint32_t e = 0; e < 2; ++e)
		{
			auto& essi = e == 0 ? m_periph.getEssi0() : m_periph.getEssi1();
			essi.setReadRxCallback([this, e](uint64_t&, dsp56k::Audio::RxFrame& _frame)
			{
				if(!m_hasUpstream)
				{
					// The first DSP has no DSP upstream: it receives the codec, the back-panel audio
					// inputs (R on ESSI0 -> X:$6C4, L on ESSI1 -> X:$6C5; from there they travel down
					// the link to the other DSPs, in channels 4 and 5).
					_frame.resize(2);
					_frame[0][0] = _frame[1][0] = static_cast<dsp56k::TWord>(m_input[e]) & 0xffffff;
					return;
				}
				readLink(e, _frame);
			});
		}

		// ESSI slot masks (TSMA/TSMB/RSMA/RSMB) at their reset value: all slots enabled. The
		// emulator leaves them at 0, and the G1's voice DSPs never write them (only DSP 3 sets
		// them): with 0 they neither transmitted nor requested data from the DMA.
		for(const dsp56k::TWord reg : {0xffffb4u, 0xffffb3u, 0xffffb2u, 0xffffb1u, 0xffffa4u, 0xffffa3u, 0xffffa2u, 0xffffa1u})
			m_periph.write(reg, 0xffffff);

		// An ESSI waiting for input audio must not block. On the hardware the codec always
		// delivers frames: here the input starts with empty ones.
		m_periph.getEssi0().writeEmptyAudioIn(64);
		m_periph.getEssi1().writeEmptyAudioIn(64);

		hdi08().setRXRateLimit(0);
		m_dsp.setInterruptServicedCallback([this](const dsp56k::TWord _vba)
		{
			++m_servicedVectors[_vba];
			m_lastVector = _vba;
			if(_vba == g_irqdVector)
				m_irqdPending = false;
		});

		// HF0/HF1 flags from the CPU's ICR to the DSP's HSR.
		m_hdiUc.setIcrWriteCallback([this](const uint8_t _icr)
		{
			hdi08().setHostFlags((_icr & mc68k::Hdi08::Hf0) ? 1 : 0, (_icr & mc68k::Hdi08::Hf1) ? 1 : 0);
		});
		m_hdiUc.setWriteIrqCallback([this](const uint8_t _vector) { hostCommand(_vector); });
		m_hdiUc.setReadIsrCallback([this](const uint8_t _isr) { return readIsr(_isr); });
		m_hdiUc.setRxEmptyCallback([this](const bool _needMoreData)
		{
			if(_needMoreData && m_booted && !m_jitDead && !hdi08().hasTX())
				runUntil(m_dsp.getCycles() + g_waitClamp);
			transferToHost();
		});
		m_hdiUc.setForceTxde(false);
		m_hdiUc.setInitHdi08Callback([this]
		{
			m_hdiUc.icr(m_hdiUc.icr() & 0x7f);
			m_hdiUc.isr(m_hdiUc.isr() | mc68k::Hdi08::IsrBits::Txde | mc68k::Hdi08::IsrBits::Trdy);
		});

		// G1_INTERP=mask: those DSPs (bit n = DSP n) run on the core's interpreter instead of the
		// JIT. Much slower; useful to tell whether a fault is the JIT's.
		m_noLaFix = std::getenv("G1_NO_LA_FIX") != nullptr;
		m_rxPerWord = std::getenv("G1_ESSI_RX_PER_WORD") != nullptr;
		m_idleSkip = std::getenv("G1_NO_IDLE_SKIP") == nullptr;	// only to compare against the bug
		if(const char* tr = std::getenv("G1_DSPTRACE"))
		{
			unsigned d = 0, pc = 0, arg = 0;
			if(std::sscanf(tr, "%u,%x,%x", &d, &pc, &arg) == 3 && d == _index)
			{
				m_traceOn = true;
				m_tracePc = pc;
				m_traceArg = arg;
				m_trace.resize(1u << 18);
			}
		}
		if(const char* in = std::getenv("G1_INTERP"))
			m_interpreter = ((std::strtoul(in, nullptr, 0) >> _index) & 1) != 0;

		armBoot();
	}

	// Back to the boot ROM state: the next words are length, address and program.
	void Dsp::armBoot()
	{
		m_booted = false;
		m_irqdPending = false;

		// What the CPU had already sent and the previous program did not read waits in the
		// port: on the hardware the boot ROM reads it, so it is handed over in order.
		std::vector<dsp56k::TWord> pending;
		auto& rx = const_cast<std::remove_const_t<std::remove_reference_t<decltype(hdi08().rxData())>>&>(hdi08().rxData());
		while(!rx.empty())
			pending.push_back(rx.pop_front());
		hdi08().clearRX();

		m_boot = std::make_unique<dsp56k::DspBoot>(m_dsp);
		m_hdiUc.setWriteTxCallback([this](const uint32_t _word)
		{
			if(!m_booted && m_boot->hdiWriteTX(_word))
				onBootFinished();
			else if(m_booted)
				hostWord(_word);
		});
		for(const auto w : pending)
		{
			if(m_booted)
				hostWord(w);
			else if(m_boot->hdiWriteTX(w))
				onBootFinished();
		}
	}

	void Dsp::onBootFinished()
	{
		m_booted = true;
		++m_bootCount;
		m_hdiUc.setWriteTxCallback([this](const uint32_t _word) { hostWord(_word); });
	}

	void Dsp::runUntil(const uint64_t _cycles)
	{
		// Dispatch once per catch-up, not per instruction. The normal loop has no
		// profiling code; only the selected DSP uses the diagnostic specialization.
		if(m_cycleProfile) runUntilImpl<true>(_cycles);
		else runUntilImpl<false>(_cycles);
	}

	void Dsp::startCycleProfile()
	{
		m_cycleProfile = std::make_unique<CycleProfile>(m_dsp);
		m_dsp.setInterruptServicedCallback([this](const dsp56k::TWord vba)
		{
			++m_servicedVectors[vba];
			m_lastVector = vba;
			if(vba == g_irqdVector) m_irqdPending = false;
			m_cycleProfile->beginVector(vba);
		});
	}

	template<bool Profile> void Dsp::runUntilImpl(const uint64_t _cycles)
	{
		// Between two catch-ups the CPU may have written the host port (HF0, which the idle loop
		// tests, changes no DSP register): what skipIdle learned is only valid within one.
		m_idle.valid = false;
		if(m_jitDead)
		{
			runDead(_cycles);
			return;
		}
		while(m_booted && m_dsp.getCycles() < _cycles)
		{
			// `jmp $FF0000`: the program returns to the boot ROM.
			if(m_dsp.getPC().toWord() >= g_bootRom)
			{
				armBoot();
				return;
			}
			if(m_traceOn)
				traceStep();
			if(!m_pcWatch.empty())
			{
				auto it = m_pcWatch.find(m_dsp.getPC().toWord());
				if(it != m_pcWatch.end()) ++it->second;
			}
			const auto before = m_dsp.getCycles();
			if(before >= m_nextIrqd)
			{
				// Fixed grid (not "now + period"): IRQD does not drift against the ESSI clock, which
				// also counts exact cycles. If it fell far behind (reload stop, boot), it re-locks
				// without a burst of interrupts. In multiples of 864 cycles: the four DSPs share the
				// grid, like on the hardware (they stay in lock-step), so the block number is common.
				m_nextIrqd = (before - m_nextIrqd > g_cyclesPerFrame * 4) ? (before / g_cyclesPerFrame + 1) * g_cyclesPerFrame : m_nextIrqd + g_cyclesPerFrame;
				if(m_hasUpstream && !m_rxPerWord)
					writeLinkRing(before / g_cyclesPerFrame);
				if(irqdEnabled())
				{
					if(m_inputProvider)
						m_inputProvider(m_input[1], m_input[0]);	// L comes in on ESSI1 and R on ESSI0
					if(m_blockCallback)
						tapBlock();
					if(m_next)
						tapLink(before / g_cyclesPerFrame);
					// IRQD is a pin: a request that arrives while the last one is still pending
					// is the same request, not a second one. The core queues interrupts, so a DSP
					// whose sample routine filled the block piled them up (83,071 on DSP 0 with
					// #4's WavetableSynth.pch, in a ring of 1024), and a host command queued behind
					// them was never reached.
					if(!m_irqdPending)
					{
						m_dsp.injectInterrupt(g_irqdVector);	// like a peripheral: does not block
						m_irqdPending = true;
						++m_irqdCount;
					}
					else
						++m_irqdOverruns;
				}
			}
			if constexpr(!Profile)
			{
				if(m_dsp.getPC().toWord() == g_idlePc && m_idleSkip && skipIdle(_cycles))
					continue;	// the checks above, again, at the new point
			}
			++m_execBlocks;
			if constexpr(Profile)
				m_cycleProfile->exec();
			else if(m_interpreter)
				m_dsp.execInterpreter();
			else
				m_dsp.exec();
			if(!m_interpreter && m_dsp.getJit().hasFailed() && !onJitFailure())
			{
				runDead(_cycles);
				return;
			}
			if(static_cast<dsp56k::TWord>(m_dsp.regs().la.var) != m_lastLa && !m_noLaFix)
				onLaChanged();
			const auto now = m_dsp.getCycles();
			if(now == before)	// DSP stopped (WAIT/STOP or halted): do not insist
			{
				++m_stalls;
				return;
			}
			if((now & 0x3ff) < now - before)	// every ~1000 cycles (less than a frame)
				drainAudio();
		}
	}

	// The OS's idle loop, $16C-$172: while X:$1 (blocks since the last control-rate pass) is 3 or less
	// it tests HF0, reads DOR1 and DCO1 into r3 and r4 and X:$1 into a, and goes round again. It
	// writes nothing, and nothing it reads changes until an interrupt (the sample clock, a peripheral)
	// or the CPU (only between catch-ups, never inside one: runUntil forgets what was learned). With a light patch a DSP spends
	// ~90 % of its instructions here, one JIT dispatch per short block. So once two whole iterations
	// have left every register as it was and cost the same, the DSP is moved on by as many whole
	// iterations as fit before the next point where anything could differ: the target, the next sample
	// clock, the next point where a peripheral is due (by instructions or by cycles), and the next
	// multiple of 1024 cycles, after which runUntil calls drainAudio (where a new CRA anchors an ESSI's
	// clock). It lands at the start of an iteration, with the registers, the instruction count and the
	// cycle count it would have had, and runs on from there: the output is byte-identical
	// (tools/bench/golden.sh). G1_NO_IDLE_SKIP turns it off.
	bool Dsp::skipIdle(const uint64_t _target)
	{
		if(m_interpreter || m_traceOn || !m_pcWatch.empty()
			|| m_dsp.getProcessingMode() != dsp56k::DSP::Default
			|| m_dsp.hasPendingInterrupts() || m_dsp.hasPendingExternalInterrupts())
		{
			m_idle.valid = false;
			return false;
		}
		const uint64_t ins = m_dsp.getInstructionCounter();
		const uint64_t cyc = m_dsp.getCycles();
		const auto& regs = m_dsp.regs();
		const bool same = m_idle.valid && std::memcmp(&m_idle.regs, &regs, sizeof(regs)) == 0;
		if(!same)
			m_idle.stable = false;
		else
		{
			const uint64_t dIns = ins - m_idle.instructions, dCyc = cyc - m_idle.cycles;
			if(m_idle.stable && dIns == m_idle.dInstructions && dCyc == m_idle.dCycles && dIns && dCyc && idleLoopInPlace())
			{
				uint64_t k = (_target - cyc) / dCyc;
				k = std::min(k, m_nextIrqd > cyc ? (m_nextIrqd - cyc) / dCyc : 0);
				const uint64_t due = m_periph.getTargetClock();
				k = std::min(k, due > ins ? (due - ins) / dIns : 0);
				// runUntil calls drainAudio after the block that crosses a multiple of 1024 cycles, and
				// drainAudio is where a new CRA anchors an ESSI's fine clock: a skip must not cross one.
				k = std::min(k, ((cyc | 0x3ff) - cyc) / dCyc);
				if(k)
				{
					m_dsp.fastForward(static_cast<dsp56k::TWord>(k * dIns), static_cast<dsp56k::TWord>(k * dCyc));
					m_idle.instructions = ins + k * dIns;
					m_idle.cycles = cyc + k * dCyc;
					++m_idleSkips;
					return true;
				}
			}
			m_idle.dInstructions = dIns;
			m_idle.dCycles = dCyc;
			m_idle.stable = true;
		}
		std::memcpy(&m_idle.regs, &regs, sizeof(regs));
		m_idle.instructions = ins;
		m_idle.cycles = cyc;
		m_idle.valid = true;
		return false;
	}

	// The loop is the OS's: the same words on the four DSPs with OS 3.03. Anything else is not skipped.
	bool Dsp::idleLoopInPlace() const
	{
		static constexpr std::array<dsp56k::TWord, 7> loop = {0x0dc323, 0xffff32, 0x085332, 0x085429, 0x56811b, 0x014385, 0x05f7da};
		for(size_t i = 0; i < loop.size(); ++i)
			if(m_memory.get(dsp56k::MemArea_P, g_idlePc + static_cast<dsp56k::TWord>(i)) != loop[i])
				return false;
		return true;
	}

	// G1_DSPTRACE: one entry per JIT block run by this DSP, and the whole ring printed once when the
	// condition is met. How the loop-end bug above was found.
	void Dsp::traceStep()
	{
		auto& r = m_dsp.regs();
		const uint32_t pc = m_dsp.getPC().toWord();
		const uint32_t r6 = static_cast<uint32_t>(r.r[6].var) & 0xffffff;
		m_trace[m_tracePos++ % m_trace.size()] = {pc, r6, static_cast<uint32_t>(r.n[6].var) & 0xffffff,
			static_cast<uint32_t>(r.sr.var) & 0xffffff, static_cast<uint32_t>(m_dsp.getProcessingMode()),
			static_cast<uint32_t>(r.sp.var) & 0x3f, static_cast<uint32_t>(r.la.var) & 0xffffff};
		if(m_tracePc == 0)
		{
			// pc 0: when one block at or above arg repeats 1000 times in a row (a DSP stuck on itself)
			m_traceRepeat = (pc == m_traceLastPc && pc >= m_traceArg) ? m_traceRepeat + 1 : 0;
			m_traceLastPc = pc;
			if(m_traceRepeat < 1000)
				return;
		}
		else if(pc != m_tracePc || r6 == m_traceArg || !irqdEnabled())
			return;	// otherwise: at pc, with R6 other than arg, while IRQD is enabled
		m_traceOn = false;
		std::printf("TRACE DSP%u at $%04x after %zu blocks. LA=$%06x; JIT loops (begin -> end):", m_index, pc, m_tracePos, static_cast<unsigned>(r.la.var));
		for(const auto& [b, e] : m_dsp.getJit().getLoops())
			std::printf(" $%04x->$%04x", static_cast<unsigned>(b), static_cast<unsigned>(e));
		std::printf("\nlast blocks (pc r6 n6 sr mode sp la):\n");
		const size_t n = std::min(m_tracePos, m_trace.size());
		for(size_t i = m_tracePos - n; i < m_tracePos; ++i)
		{
			const auto& t = m_trace[i % m_trace.size()];
			std::printf("  %04x %06x %06x %06x m%u sp%02x la%04x\n", t.pc, t.r6, t.n6, t.sr, t.mode, t.sp, t.la);
		}
	}

	// IRQD only counts if the program enables it in the IPRC (X:$FFFFFF, IDL level in bits
	// 9-10; 0 = disabled) and the SR mask lets it through. The emulator only checks the SR:
	// during the reload stop (IPRC=$FF0800) the G1 disables it on purpose.
	bool Dsp::irqdEnabled()
	{
		const auto iprc = m_periph.read(0xffffff, dsp56k::Instruction::Invalid);
		return ((iprc >> 9) & 3) != 0 && !m_dsp.isInterruptMasked(g_irqdVector);
	}

	// What this DSP sent through its two ESSIs in the previous block: 9 words per ESSI from
	// X:$5 and X:$6 (the same buffers as tapBlock), for the next DSP.
	void Dsp::tapLink(const uint64_t _block)
	{
		auto& mem = m_dsp.memory();
		const auto p0 = mem.get(dsp56k::MemArea_X, 5);
		const auto p1 = mem.get(dsp56k::MemArea_X, 6);
		if(p0 < 0x600 || p0 > 0x7f7 || p1 < 0x600 || p1 > 0x7f7 || _block == 0)
			return;
		LinkBlock b;
		b.index = _block - 1;
		for(dsp56k::TWord i = 0; i < 9; ++i)
		{
			b.words[i] = mem.get(dsp56k::MemArea_Y, p0 + i);
			b.words[9 + i] = mem.get(dsp56k::MemArea_Y, p1 + i);
		}
		for(size_t i = 0; i < b.words.size(); ++i)
		{
			const auto v = static_cast<int32_t>(b.words[i] << 8) >> 8;
			m_linkPeak[i] = std::max<uint32_t>(m_linkPeak[i], static_cast<uint32_t>(v < 0 ? -v : v));
		}
		m_linkOut.push_back(b);
	}

	// The previous block's output, before the next one starts. The block routine alternates its
	// buffers ($6C0/$6E0) and leaves in X:$5 and X:$6 the ones it just sent through DMA4 (ESSI0)
	// and DMA5 (ESSI1): two words each. Reading them here gives exactly one sample per block,
	// regardless of how the ESSI splits it into frames.
	void Dsp::tapBlock()
	{
		auto& mem = m_dsp.memory();
		const auto p0 = mem.get(dsp56k::MemArea_X, 5);
		const auto p1 = mem.get(dsp56k::MemArea_X, 6);
		if(p0 < 0x600 || p0 > 0x7fe || p1 < 0x600 || p1 > 0x7fe)
			return;	// the sound program is not running yet
		BlockFrame f;
		// Each ESSI carries its pair reversed: first the even output (2 and 4), then the odd one
		// (1 and 3). Checked with a 4Output and a different signal on each output.
		f[0] = mem.get(dsp56k::MemArea_Y, p0 + 1);	// output 1
		f[1] = mem.get(dsp56k::MemArea_Y, p0);		// output 2
		f[2] = mem.get(dsp56k::MemArea_Y, p1 + 1);	// output 3
		f[3] = mem.get(dsp56k::MemArea_Y, p1);		// output 4
		m_blocks.push_back(f);
	}

	void Dsp::readLink(const uint32_t _essi, dsp56k::Audio::RxFrame& _frame)
	{
		_frame.resize(2);
		const auto ddr = m_periph.getDMA().getDDR(2 + _essi);
		const bool inRing = ddr >= g_linkBase[_essi] && ddr < g_linkBase[_essi] + 9;
		// The frame is requested at slot 0, but slot 1 comes 96 cycles later and may already fall
		// into the next block (9 words per block, 2 per frame): each word is taken from the block
		// in which it will be received.
		for(uint32_t s = 0; s < 2; ++s)
		{
			// When the slot is due, not when the DSP got round to it: near a block's edge that
			// difference picks the wrong block.
			const auto want = (m_periph.getEssiClock().getSlotClock() + s * g_linkCyclesPerWord) / g_cyclesPerFrame;
			const auto block = want > g_linkLatency ? want - g_linkLatency : 0;
			// Blocks no longer needed are dropped. If the one needed is missing (the previous DSP
			// is stopped reloading, without IRQD), silence: repeating an old one would buzz.
			while(m_linkIn.size() > 1 && m_linkIn[1].index <= block)
				m_linkIn.pop_front();
			const LinkBlock* src = (!m_linkIn.empty() && m_linkIn.front().index == block) ? &m_linkIn.front() : nullptr;
			// Late: the block wanted is newer than any handed on yet, so the previous DSP's audio
			// came too late (issue #65). Skipped blocks (a reload stop) are not counted.
			if(!src && inRing && m_hasUpstream && block > 0 && (m_linkIn.empty() || m_linkIn.back().index < block))
				m_linkMisses.fetch_add(1, std::memory_order_relaxed);
			const auto pos = (ddr - g_linkBase[_essi] + s) % 9;
			_frame[s][0] = (src && inRing) ? src->words[_essi * 9 + pos] : 0;
		}
	}

	// The link from the DSP upstream, without the ESSI receivers. Their DMA writes each block's word i to
	// base+i, and does it during the sample after the one the block was due in: so at the sample clock of
	// sample n the ring holds block n - g_linkLatency - 1, whole. Here that block is written at once,
	// before the sample routine starts, instead of word by word through the ESSI and the DMA (a quarter of
	// a DSP thread's time). Nothing is written while the receive DMA does not point at the ring, as the
	// DMA would not have written there either.
	void Dsp::writeLinkRing(const uint64_t _sample)
	{
		const uint64_t block = _sample > g_linkLatency ? _sample - g_linkLatency - 1 : 0;
		while(m_linkIn.size() > 1 && m_linkIn[1].index <= block)
			m_linkIn.pop_front();
		const LinkBlock* src = (!m_linkIn.empty() && m_linkIn.front().index == block) ? &m_linkIn.front() : nullptr;
		auto& dma = m_periph.getDMA();
		auto& mem = m_dsp.memory();
		for(uint32_t e = 0; e < 2; ++e)
		{
			const auto ddr = dma.getDDR(2 + e);
			if(ddr < g_linkBase[e] || ddr >= g_linkBase[e] + 9)
				continue;
			// Late, as in readLink: the block wanted is newer than any handed on yet (issue #65).
			if(!src && block > 0 && (m_linkIn.empty() || m_linkIn.back().index < block))
				m_linkMisses.fetch_add(1, std::memory_order_relaxed);
			for(dsp56k::TWord i = 0; i < 9; ++i)
				mem.set(dsp56k::MemArea_X, g_linkBase[e] + i, src ? src->words[e * 9 + i] : 0);
		}
	}

	// The OS extends the main loop without rewriting the DO: when it loads a patch with
	// control-rate modules (envelopes, clocks, master oscillators...) it puts their code at the
	// end of the loop (from $174) and changes the LA register with a host command (vector $7C:
	// movep ...,la). On the DSP the loop end is compared with LA on every pass; Gearmulator's
	// JIT, instead, records the end when it compiles the DO and cuts its blocks there. Without
	// this, only the first instruction of the control code ran and all control rate stood still.
	void Dsp::onLaChanged()
	{
		const dsp56k::TWord oldLa = m_lastLa;
		const dsp56k::TWord newLa = static_cast<dsp56k::TWord>(m_dsp.regs().la.var);
		m_lastLa = newLa;
		auto& jit = m_dsp.getJit();
		constexpr dsp56k::TWord none = 0xffffffff;
		dsp56k::TWord begin = none;
		for(const auto& [b, end] : jit.getLoops())
			if(end == oldLa + 1)
				begin = b;
		if(begin == none)
			return;	// that loop is not compiled: the JIT will record it correctly when it compiles it
		jit.removeLoop(begin);
		jit.addLoop(begin, newLa + 1);
		// Blocks that ended at the old end or run through the new one are recompiled.
		for(const dsp56k::TWord pc : std::array<dsp56k::TWord, 4>{oldLa, oldLa + 1, newLa, newLa + 1})
			jit.destroy(pc);
		++m_laChanges;
	}

	bool Dsp::onJitFailure()
	{
		auto& jit = m_dsp.getJit();
		const auto reason = jit.failReason();
		jit.clearFailure();
		// Out of JIT memory is the likely cause, and throwing every block away frees it all. One
		// retry per second of DSP time: a failure that comes straight back is not passing.
		const auto now = m_dsp.getCycles();
		if(m_jitRecoveries == 0 || now - m_lastJitRecovery > g_dspClock)
		{
			++m_jitRecoveries;
			m_lastJitRecovery = now;
			std::fprintf(stderr, "G1-Emu: DSP %u: the JIT could not generate a block (%s); emptying its cache and going on\n", m_index, reason.c_str());
			jit.destroyAllBlocks();
			return true;
		}
		m_jitDead = true;
		m_jitFailure = reason;
		m_deadCycles = now;
		std::fprintf(stderr, "G1-Emu: DSP %u: the JIT keeps failing (%s); this DSP is stopped and gives silence\n", m_index, reason.c_str());
		return false;
	}

	void Dsp::runDead(const uint64_t _cycles)
	{
		// The DSP's own clock no longer moves. Its sample clock still does, here, so whoever waits
		// for its audio (the output, the next DSP) gets silence on time and nothing stalls. A
		// silent output carries DSP 3's X:$5F offset, as a running one does.
		constexpr dsp56k::TWord silence = 0x155;
		while(m_deadCycles + g_cyclesPerFrame <= _cycles)
		{
			m_deadCycles += g_cyclesPerFrame;
			if(m_blockCallback)
				m_blocks.push_back({silence, silence, silence, silence});
		}
	}

	void Dsp::catchUp(const uint64_t _cycles, const bool _toHost)
	{
		runUntil(_cycles);
		drainAudio();
		if(_toHost)
			transferToHost();
	}

	// Called by DSP 0's ESSI clock after every tick. Its codec receivers take a word every 4.5 ticks,
	// which a divider cannot say: they run every 4 ticks, and once per sample (9 ticks) their counter
	// is set one tick back, so the next word waits 5. Two words per 864 cycles, spaced 5 and 4. The
	// transmitters have divider 0, so their counter is 0 after every tick and setting it changes nothing.
	void Dsp::codecTick()
	{
		if(++m_codecTicks < g_ticksPerSample)
			return;
		m_codecTicks = 0;
		auto& clock = m_periph.getEssiClock();
		for(auto* essi : {&m_periph.getEssi0(), &m_periph.getEssi1()})
			clock.setEsaiCounter(essi, 0, -1);
	}

	// Collects what left the ESSIs (for flushAudio) and measures the peaks.
	void Dsp::drainAudio()
	{
		uint32_t e = 0;
		for(auto* essi : {&m_periph.getEssi0(), &m_periph.getEssi1()})
		{
			const dsp56k::TWord cra = essi->getCRA();
			if(cra != m_craSeen[e])
			{
				m_craSeen[e] = cra;
				uint32_t tx = essiWordCycles(cra), rx = tx;
				if(m_hasUpstream)
				{
					// The receiver takes the upstream DSP's link clock, whatever its own CRA says
					// (g_linkCyclesPerWord); DSP 3's codec transmitter goes with it.
					if(tx < g_essiBaseCyclesPerWord)
						tx = rx = g_linkCyclesPerWord;
				}
				const auto divider = [](const uint32_t _cycles)
				{
					return std::max<uint32_t>(1, (_cycles + g_essiTickCycles / 2) / g_essiTickCycles) - 1;
				};
				// What the ESSIs transmit is never used: the links and the outputs are read from the
				// DSP's memory (tapLink, tapBlock). Serving the transmitters once per sample instead of
				// once per word keeps them running at the sample rate, in case the OS looks at their
				// flags, and makes the emulator ~30 % faster (G1_ESSI_TX_PER_WORD=1: every word).
				static const bool txPerWord = std::getenv("G1_ESSI_TX_PER_WORD") != nullptr;
				if(!txPerWord)
					tx = g_cyclesPerFrame;
				// The same goes for what they receive from the DSP upstream: writeLinkRing puts each
				// block straight into the receive ring once per sample, so the receivers are never
				// served (their DMA has no interrupt and the OS reads only the ring).
				if(m_hasUpstream)
					m_periph.getEssiClock().setEsaiDivider(essi, divider(tx), m_rxPerWord ? divider(rx) : g_rxNeverServed);
				else
				{
					// DSP 0's codec receiver supplies two words per 96 kHz sample, one every 432
					// cycles: 4 ticks, and a fifth every other word (codecTick). The same ESSI still
					// transmits the 96-cycle DSP link to DSP 1.
					m_periph.getEssiClock().setEsaiDivider(essi, divider(tx), g_codecRxTicks - 1);
					m_periph.getEssiClock().setTickCallback([this] { codecTick(); });
				}
			}
			auto& out = essi->getAudioOutputs();
			auto& meter = m_meter[e];
			auto& slotCount = m_slotCount[e];
			while(!out.empty())
			{
				out.pop_front([&](const auto& _frame)
				{
					slotCount = _frame.size();
					StagedFrame f{static_cast<uint32_t>(std::min<size_t>(_frame.size(), 4)), {}};
					for(uint32_t s = 0; s < f.slots; ++s)
						f.v[s] = _frame[s][0];
					m_staged[e].push_back(f);
					for(uint32_t s = 0; s < std::min<uint32_t>(_frame.size(), MeterSlots); ++s)
						for(uint32_t l = 0; l < MeterLines; ++l)
						{
							auto v = static_cast<int32_t>(_frame[s][l] << 8) >> 8;	// signed 24-bit
							const auto a = static_cast<uint32_t>(v < 0 ? -v : v);
							if(a > meter[s][l])
								meter[s][l] = a;
						}
				});
				++m_audioFrames;
			}
			++e;
		}
	}

	void Dsp::flushAudio()
	{
		// Both ESSIs interleaved, frame by frame, as they left the DSP.
		const auto n = std::max(m_staged[0].size(), m_staged[1].size());
		for(size_t k = 0; k < n; ++k)
		{
			for(uint32_t e = 0; e < 2; ++e)
			{
				if(k >= m_staged[e].size())
					continue;
				const auto& f = m_staged[e][k];
				if(m_audioCallback && f.slots >= 2)
					m_audioCallback(e, static_cast<int32_t>(f.v[0] << 8) >> 8, static_cast<int32_t>(f.v[1] << 8) >> 8);
			}
		}
		m_staged[0].clear();
		m_staged[1].clear();

		if(m_next)
		{
			for(const auto& b : m_linkOut)
				m_next->m_linkIn.push_back(b);
			m_chainedFrames += m_linkOut.size();
			m_linkOut.clear();
			// Cap in case the next DSP does not consume (stopped): about 100 ms.
			while(m_next->m_linkIn.size() > 10000)
				m_next->m_linkIn.pop_front();
		}

		if(m_blockCallback)
			for(const auto& f : m_blocks)
			{
				auto s24 = [](const dsp56k::TWord _v) { return static_cast<int32_t>(_v << 8) >> 8; };
				m_blockCallback(s24(f[0]), s24(f[1]), s24(f[2]), s24(f[3]));
			}
		m_blocks.clear();
	}

	void Dsp::hostWord(const uint32_t _word)
	{
		// HRX holds a single word: if the previous one is still there, let the DSP run.
		const auto stop = m_dsp.getCycles() + g_waitClamp;
		while(hdi08().hasRXData() && m_booted && !m_jitDead && m_dsp.getCycles() < stop)
			runUntil(m_dsp.getCycles() + 64);
		// If meanwhile the program went back to the boot ROM, the word belongs to it.
		if(!m_booted)
		{
			if(m_boot->hdiWriteTX(_word))
				onBootFinished();
			return;
		}
		hdi08().writeRX(&_word, 1);
		++m_hostWords;
	}

	void Dsp::hostCommand(const uint8_t _vector)
	{
		if(!m_booted)
			return;
		// The real DSP services each host command as soon as it arrives. Here it is allowed to run
		// until the external interrupt queue (32 entries) is empty: otherwise it fills up and
		// injectExternalInterrupt waits forever. Only that queue: hasPendingInterrupts() also counts
		// the DSP being inside an interrupt, and a DSP with almost no idle time (nmedit's korg.pch:
		// its sample routine fills the block) never leaves it for the 200,000 cycles allowed, so the
		// command was dropped, and the 68k polled the HI08 for an answer that never came.
		const auto stop = m_dsp.getCycles() + g_waitClamp;
		while(m_dsp.hasPendingExternalInterrupts() && m_booted && !m_jitDead && m_dsp.getCycles() < stop)
			runUntil(m_dsp.getCycles() + 16);
		if(m_dsp.hasPendingExternalInterrupts())
			return;	// the DSP does not service them (stopped): better to lose the command than hang
		hdi08().injectHostCommand(_vector);
		++m_hostCommands;
		transferToHost();
	}

	uint8_t Dsp::readIsr(uint8_t _isr)
	{
		// On the hardware the DSP takes the incoming word at once; here it may lag. If the CPU
		// polls with a word pending, the DSP runs until it takes it: the OS routine drops the word
		// after 10 polls. Short wait: if the DSP is in a loop that does not read the port (for
		// example stopped with HF2 up waiting for the CPU to lower HF0), the poll returns the status
		// as it is.
		if(m_booted && hdi08().hasRXData())
		{
			const auto stop = m_dsp.getCycles() + g_isrWaitClamp;
			while(hdi08().hasRXData() && m_booted && !m_jitDead && m_dsp.getCycles() < stop)
				runUntil(m_dsp.getCycles() + 64);
		}
		transferToHost();
		_isr = static_cast<uint8_t>((_isr & ~mc68k::Hdi08::Rxdf) | (m_hdiUc.canReceiveData() ? 0 : mc68k::Hdi08::Rxdf));

		// HF2/HF3 from the DSP to the CPU's ISR.
		const auto hf23 = hdi08().readControlRegister() & 0x18;
		_isr = static_cast<uint8_t>((_isr & ~0x18) | hf23);

		// TXDE: room for another word. During boot, the ROM always accepts.
		_isr &= static_cast<uint8_t>(~(mc68k::Hdi08::Txde | mc68k::Hdi08::Trdy));
		const auto depth = m_booted ? hdi08().rxData().size() : 0;
		if(depth == 0)
			_isr |= mc68k::Hdi08::Txde | mc68k::Hdi08::Trdy;
		else if(depth == 1)
			_isr |= mc68k::Hdi08::Txde;
		return _isr;
	}

	bool Dsp::transferToHost()
	{
		if(m_hdiUc.canReceiveData() && hdi08().hasTX())
		{
			m_hdiUc.writeRx(hdi08().readTX());
			++m_wordsToHost;
			return true;
		}
		return false;
	}
}
