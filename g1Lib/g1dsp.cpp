#include "g1dsp.h"

#include "mc68k/hdi08.h"
#include "dsp56kEmu/jit.h"

#include <algorithm>
#include <cstdlib>
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

		// The ESSIs run on the clock derived from their CRA (the core's "fine link" mode):
		// 2*(PM+1)*24 cycles per word, 96 on the links between DSPs (PM=1: 9 words per sample,
		// exactly what DMA4 sends each block) and 144 on DSP 3 towards the codec (PM=2). The base
		// clock is only an upper bound: it has to be slower than both.
		constexpr uint32_t g_essiBaseCyclesPerWord = g_cyclesPerFrame / 2;

		// In asynchronous mode the receiver uses the transmitter's clock (external SC0), not its
		// own CRA. Every DSP transmitting down the chain has PM=1: 96 cycles per word. DSP 3 has
		// PM=2 because its CRA is set for the codec: its receiver would run at 144 and could only
		// take 6 of the 9 words of each sample.
		constexpr uint32_t g_linkCyclesPerWord = 96;

		// Link delay between DSPs, in blocks. The threads sync every ~4,000 cycles (about 5
		// blocks): the receiver takes the block from g_linkLatency blocks ago, which has surely
		// arrived. On the hardware it is less than a block; here it is ~83 us per DSP.
		constexpr uint64_t g_linkLatency = 8;
		// Each ESSI's receive DMA writes into a 9-word ring: X:$6C0 (ESSI0) and X:$6C9 (ESSI1).
		// Word i of the block always goes to base+i.
		constexpr dsp56k::TWord g_linkBase[2] = {0x6c0, 0x6c9};

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
		config.maxDoIterations = 1;
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
		clock.setCyclesPerSample(g_essiBaseCyclesPerWord);

		// Must be enabled before the program writes CRA.
		for(auto* essi : {&m_periph.getEssi0(), &m_periph.getEssi1()})
			essi->setFineLinkMode(true);

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
		// No arbitration: the G1's host commands are fast interrupts (a single movep in the vector,
		// no JSR/RTI). Gearmulator's arbitration waits for the RTI on the stack to consider the
		// command done, and with these it stayed "busy" forever.
		hdi08().setHostCommandArbitration(false);
		m_dsp.setInterruptServicedCallback([this](const dsp56k::TWord _vba)
		{
			++m_servicedVectors[_vba];
			m_lastVector = _vba;
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
			if(_needMoreData && m_booted && !hdi08().hasTX())
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
		m_noLaFix = std::getenv("G1_NO_LA_FIX") != nullptr;	// only to compare against the bug
		if(const char* in = std::getenv("G1_INTERP"))
			m_interpreter = ((std::strtoul(in, nullptr, 0) >> _index) & 1) != 0;

		armBoot();
	}

	// Back to the boot ROM state: the next words are length, address and program.
	void Dsp::armBoot()
	{
		m_booted = false;

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
		while(m_booted && m_dsp.getCycles() < _cycles)
		{
			// `jmp $FF0000`: the program returns to the boot ROM.
			if(m_dsp.getPC().toWord() >= g_bootRom)
			{
				armBoot();
				return;
			}
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
				if(irqdEnabled())
				{
					if(m_inputProvider)
						m_inputProvider(m_input[1], m_input[0]);	// L comes in on ESSI1 and R on ESSI0
					if(m_blockCallback)
						tapBlock();
					if(m_next)
						tapLink(before / g_cyclesPerFrame);
					m_dsp.injectInterrupt(g_irqdVector);	// like a peripheral: does not block
					++m_irqdCount;
				}
			}
			if(m_interpreter)
				m_dsp.execInterpreter();
			else
				m_dsp.exec();
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
			const auto want = (m_dsp.getCycles() + s * g_linkCyclesPerWord) / g_cyclesPerFrame;
			const auto block = want > g_linkLatency ? want - g_linkLatency : 0;
			// Blocks no longer needed are dropped. If the one needed is missing (the previous DSP
			// is stopped reloading, without IRQD), silence: repeating an old one would buzz.
			while(m_linkIn.size() > 1 && m_linkIn[1].index <= block)
				m_linkIn.pop_front();
			const LinkBlock* src = (!m_linkIn.empty() && m_linkIn.front().index == block) ? &m_linkIn.front() : nullptr;
			const auto pos = (ddr - g_linkBase[_essi] + s) % 9;
			_frame[s][0] = (src && inRing) ? src->words[_essi * 9 + pos] : 0;
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

	void Dsp::catchUp(const uint64_t _cycles)
	{
		runUntil(_cycles);
		drainAudio();
		transferToHost();
	}

	// Collects what left the ESSIs (for flushAudio) and measures the peaks.
	void Dsp::drainAudio()
	{
		uint32_t e = 0;
		for(auto* essi : {&m_periph.getEssi0(), &m_periph.getEssi1()})
		{
			// The receiver of a DSP with another one upstream runs at its transmitter's rate.
			if(m_hasUpstream)
			{
				const dsp56k::TWord cra = essi->getCRA();
				if(cra != m_craSeen[e])
				{
					m_craSeen[e] = cra;
					if(essiWordCycles(cra) != g_linkCyclesPerWord && essiWordCycles(cra) < g_essiBaseCyclesPerWord)
						m_periph.getEssiClock().setEsaiFinePeriod(essi, g_linkCyclesPerWord);
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
		while(hdi08().hasRXData() && m_booted && m_dsp.getCycles() < stop)
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
		// until it has dispatched what is pending: otherwise the external interrupt queue
		// (32 entries) fills up and injectExternalInterrupt waits forever.
		const auto stop = m_dsp.getCycles() + g_waitClamp;
		while(m_dsp.hasPendingInterrupts() && m_booted && m_dsp.getCycles() < stop)
			runUntil(m_dsp.getCycles() + 16);
		if(m_dsp.hasPendingInterrupts())
			return;	// the DSP does not service them (stopped): better to lose the command than hang
		hdi08().writeHostCommand(_vector);
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
			while(hdi08().hasRXData() && m_booted && m_dsp.getCycles() < stop)
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
