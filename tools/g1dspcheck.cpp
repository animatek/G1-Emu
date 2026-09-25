// Regression tests for the core G1-Emu uses. Synthetic programs, no ROM.
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/essi.h"
#include "dsp56kEmu/esaiclock.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kBase/logging.h"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <stdexcept>
#include <string>

namespace
{
	void require(bool condition, const char* message)
	{
		if(!condition) throw std::runtime_error(message);
	}

	struct Machine
	{
		dsp56k::DefaultMemoryValidator validator;
		dsp56k::Memory memory{validator, 0x1000, 0x1000, 0x800};
		dsp56k::PeripheralsNop x, y;
		dsp56k::DSP dsp{memory, &x, &y};

		explicit Machine(uint32_t blockSize)
		{
			auto config = dsp.getJit().getConfig();
			config.linkJitBlocks = false;
			config.dynamicFastInterrupts = true;
			config.maxInstructionsPerBlock = blockSize;
			config.maxDoIterations = 1;
			dsp.getJit().setConfig(config);
			// writeReg does not implement SR; also update the JIT's cached mode.
			dsp.regs().sr.var = 0;
			dsp.getJit().checkModeChange();
		}

		void program(uint32_t address, std::initializer_list<uint32_t> words)
		{
			for(const auto word : words) dsp.memWriteP(address++, word);
		}

		void until(uint32_t pc)
		{
			for(unsigned i = 0; dsp.getPC().toWord() != pc && i < 100; ++i) dsp.exec();
			require(dsp.getPC().toWord() == pc, "expected PC not reached");
		}
	};

	void basicJit(uint32_t blockSize)
	{
		Machine m(blockSize);
		// nop; jmp $102
		m.program(0x100, {0, 0x0c0102, 0x0c0102});
		m.dsp.setPC(0x100);
		m.until(0x102);
	}

	void shortProgramMove(uint32_t blockSize)
	{
		Machine m(blockSize);
		// move #>$175,ssh; move ssh,p:<$17; move p:<$17,x0; jmp $105
		m.program(0x100, {0x05f43c, 0x175, 0x07173c, 0x079704, 0x0c0105, 0x0c0105});
		m.dsp.setPC(0x100);
		m.until(0x105);
		require(m.memory.get(dsp56k::MemArea_P, 0x17) == 0x175, "MOVEM does not write P:$17");
		require(m.dsp.regs().x.var == 0x175, "MOVEM does not read P:$17");
		require(m.dsp.regs().sp.var == 0, "MOVEM SSH does not balance the stack");
	}

	void invalidateProgramMove(uint32_t blockSize)
	{
		Machine m(blockSize);
		// First cache the code that MOVEM rewrites afterwards.
		m.program(0x30, {0x241100, 0x0c0200}); // move #$11,x0; jmp $200
		m.program(0x200, {0x0c0200});
		m.dsp.setPC(0x30);
		m.until(0x200);
		const auto before = m.dsp.regs().x.var;
		// move #>$242200,x1; move x1,p:<$30; jmp $30
		m.program(0x100, {0x45f400, 0x242200, 0x073005, 0x0c0030});
		m.dsp.setPC(0x100);
		m.until(0x200);
		require(m.memory.get(dsp56k::MemArea_P, 0x30) == 0x242200, "MOVEM does not rewrite the code");
		require((m.dsp.regs().x.var & 0xffffff) != (before & 0xffffff), "MOVEM leaves stale JIT code");
	}

	void foreverLoop(uint32_t blockSize, uint32_t lc)
	{
		Machine m(blockSize);
		// do forever,$104; inc a; nop; nop
		m.program(0x100, {0x000203, 0x104, 0x000008, 0, 0, 0x0c0105});
		m.dsp.regs().lc.var = lc;
		m.dsp.setPC(0x100);
		for(unsigned i = 0; i < 100; ++i) m.dsp.exec();
		require(m.dsp.getPC().toWord() >= 0x102 && m.dsp.getPC().toWord() <= 0x104, "DO FOREVER leaves the loop");
		require(m.dsp.regs().lc.var == lc, "DO FOREVER modifies LC");
		require(m.dsp.regs().sp.var == 2, "DO FOREVER does not keep its context on the stack");
		require(m.dsp.regs().a.var > 10, "DO FOREVER does not repeat the body");
		// A long IRQ must return to the loop, keeping its two stack entries.
		m.program(0x16, {0x0bf080, 0x180});
		m.program(0x180, {0x000009, 0x000004}); // inc b; rti
		m.dsp.injectInterrupt(0x16);
		for(unsigned i = 0; i < 30; ++i) m.dsp.exec();
		// B has an internal offset; check the architectural register B0.
		dsp56k::TReg24 b0;
		m.dsp.readReg(dsp56k::Reg_B0, b0);
		require(b0.var == 1, "IRQD does not run the handler");
		require(m.dsp.regs().sp.var == 2, "IRQD unbalances DO FOREVER");
		require(m.dsp.getPC().toWord() >= 0x102 && m.dsp.getPC().toWord() <= 0x104, "RTI does not return to DO FOREVER");
	}

	void finiteLoop(uint32_t blockSize)
	{
		Machine m(blockSize);
		// do #2,$102; inc a; jmp $105
		m.program(0x100, {0x060280, 0x102, 0x000008, 0x0c0105, 0, 0x0c0105});
		m.dsp.setPC(0x100);
		m.until(0x105);
		require(m.dsp.regs().a.var > 1, "finite DO does not repeat the body");
		require(m.dsp.regs().sp.var == 0, "finite DO does not restore the stack");
	}

	void nestedFiniteLoop(uint32_t blockSize)
	{
		Machine m(blockSize);
		// Outer DO #2, inner DO #2; both must restore their loop context.
		m.program(0x100, {0x060280, 0x106, 0x060280, 0x104, 0x000008, 0x000009, 0, 0x0c0109, 0, 0x0c0109});
		m.dsp.setPC(0x100);
		m.until(0x109);
		require(m.dsp.regs().a.var > 3 && m.dsp.regs().b.var > 1, "nested finite DO does not finish");
		require(m.dsp.regs().sp.var == 0, "nested finite DO does not restore the stack");
	}

	void nestedLoop(uint32_t blockSize)
	{
		Machine m(blockSize);
		// Outer DO FOREVER, inner DO #2. The end of the inner one must restore FV.
		m.program(0x100, {0x000203, 0x106, 0x060280, 0x104, 0x000008, 0x000009, 0, 0x0c0107});
		m.dsp.setPC(0x100);
		for(unsigned i = 0; i < 200; ++i) m.dsp.exec();
		require(m.dsp.regs().a.var > 10 && m.dsp.regs().b.var > 5, "the nested DO does not finish or loses FV");
		require(m.dsp.regs().sp.var == 2 || m.dsp.regs().sp.var == 4, "nested DO corrupts the stack");
		require(m.dsp.getPC().toWord() >= 0x102 && m.dsp.getPC().toWord() <= 0x106, "the outer DO stops repeating");
	}
}

	// CMPM takes absolute values to compare; it must not leave them in the accumulator it read.
	// The JIT did, on its cached copy, so the next instruction of the same block saw |a|: this is
	// what silenced every sawtooth (the G1's saw does cmpm a,b then tgt a,b).
	void cmpmKeepsOperand(uint32_t blockSize)
	{
		Machine m(blockSize);
		// cmpm a,b; tfr a,b; jmp $102
		m.program(0x100, {0x20000f, 0x200009, 0x0c0102});
		const uint64_t a = 0xff80000000000000ull;	// negative, left-aligned
		m.dsp.regs().a.var = a;
		m.dsp.regs().b.var = 0x0010000000000000ull;
		m.dsp.setPC(0x100);
		m.until(0x102);
		require(m.dsp.regs().a.var == a, "CMPM changes the accumulator it compares");
		require(m.dsp.regs().b.var == a, "the instruction after CMPM reads |a| instead of a");
	}

	// GT is Z + (N ^ V) == 0, LE the opposite. The x86-64 JIT used the parity of Z, N and V,
	// which is wrong exactly when Z = 1 and N != V.
	void greaterThanWithZero(uint32_t blockSize)
	{
		Machine m(blockSize);
		// tgt x1,a; add x0,a ifle; jmp $102
		m.program(0x100, {0x027060, 0x202f40, 0x0c0102});
		m.dsp.regs().x.var = 0x100000000001ull;	// x1 = $100000, x0 = $000001
		m.dsp.regs().a.var = 0;
		m.dsp.regs().sr.var = 0x0c;	// Z = 1, N = 1, V = 0: GT false, LE true
		m.dsp.getJit().checkModeChange();
		m.dsp.setPC(0x100);
		m.until(0x102);
		require(m.dsp.regs().a.var == 0x0000000100000000ull, "GT or LE wrong with Z set and N != V");
	}

	// An ESSI on the fine schedule (the links between DSPs) that sat idle owes nothing for that
	// time. Without the overlay's cap the clock emitted every frame of the gap in one call: on a
	// real patch (nmedit's korg.pch) DSP 0 woke up after 400 million cycles and pushed millions of
	// frames into a ring of 32768, and the write callback waited for room forever. The callback
	// here only counts, so a regression fails the test instead of hanging it.
	void idleLinkPort()
	{
		dsp56k::DefaultMemoryValidator validator;
		dsp56k::Memory memory{validator, 0x1000, 0x1000, 0x800};
		dsp56k::Peripherals56303 periph;
		dsp56k::PeripheralsNop nop;
		dsp56k::DSP dsp{memory, &periph, &nop};

		auto& clock = periph.getEssiClock();
		clock.setClockSource(dsp56k::EsxiClock::ClockSource::Cycles);
		clock.setSamplerate(96000 * 2);
		clock.setCyclesPerSample(432);

		auto& essi = periph.getEssi0();
		essi.setFineLinkMode(true);
		uint64_t frames = 0;
		essi.setWriteTxCallback([&frames](uint64_t&, const dsp56k::Audio::TxFrame&) { ++frames; });
		essi.setReadRxCallback([](uint64_t&, dsp56k::Audio::RxFrame& _frame) { _frame.resize(2); });	// the default waits for input
		essi.writeCRA(0x181801);	// 96 cycles per word: the link between DSPs
		essi.writeCRB(0x0328b0);	// transmitter on
		require(essi.hasEnabledTransmitters() != 0, "the transmitter did not turn on");

		// Running normally: a frame is two words, so one every 192 cycles: 50 in 100 word periods.
		for(uint32_t i = 0; i < 100; ++i)
		{
			dsp.fastForward(0, 96);
			clock.exec();
		}
		require(frames >= 48 && frames <= 52, "a port running normally must emit one frame per two word periods");

		// Idle for 400 million cycles (nothing served the clock), then it wakes up.
		frames = 0;
		dsp.fastForward(0, 400000000);
		clock.exec();
		require(frames <= 64, "an idle port emitted the frames of its whole idle time at once");

		// And it carries on at the normal rate afterwards.
		frames = 0;
		for(uint32_t i = 0; i < 100; ++i)
		{
			dsp.fastForward(0, 96);
			clock.exec();
		}
		require(frames >= 48 && frames <= 52, "the port did not go back to its normal rate");
	}

int main()
{
	// The core reports JIT errors (bad encodings, blocks it could not emit) through its log, which
	// by default goes to stdout unflushed: when a broken block then kills the process, the reason
	// is lost. Send it to stderr, flushed line by line, so a CI log keeps it.
	Logging::setLogFunc([](const std::string& line)
	{
		std::fprintf(stderr, "CORE: %s\n", line.c_str());
		std::fflush(stderr);
	});

	try
	{
		for(const auto blockSize : {1u, 32u})
		{
			auto run = [blockSize](const char* name, const auto& test)
			{
				std::fprintf(stderr, "RUN: %s (block size %u)\n", name, blockSize);
				if(std::getenv("GITHUB_ACTIONS"))
					std::fprintf(stderr, "::notice title=DSP test case::%s (block size %u)\n", name, blockSize);
				test();
			};

			run("basic JIT", [=] { basicJit(blockSize); });
			run("short MOVEM", [=] { shortProgramMove(blockSize); });
			run("MOVEM JIT invalidation", [=] { invalidateProgramMove(blockSize); });
			run("DO FOREVER with LC=0", [=] { foreverLoop(blockSize, 0); });
			run("DO FOREVER with LC=7", [=] { foreverLoop(blockSize, 7); });
			run("finite DO", [=] { finiteLoop(blockSize); });
			run("nested finite DO", [=] { nestedFiniteLoop(blockSize); });
			run("DO FOREVER with nested DO", [=] { nestedLoop(blockSize); });
			run("CMPM keeps its operand", [=] { cmpmKeepsOperand(blockSize); });
			run("GT and LE with Z set", [=] { greaterThanWithZero(blockSize); });
			run("idle link port", [=] { idleLinkPort(); });
		}
		std::puts("OK: short MOVEM, JIT invalidation, DO FOREVER, IRQD, nested DO, CMPM, GT/LE and an idle link port (blocks 1/32)");
		return 0;
	}
	catch(const std::exception& error)
	{
		std::fprintf(stderr, "ERROR: %s\n", error.what());
		return 1;
	}
}
