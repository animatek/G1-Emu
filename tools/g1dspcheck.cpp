// Regresiones del nucleo que usa G1-Emu. Programas sinteticos, sin ROM.
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

#include <cstdio>
#include <initializer_list>
#include <stdexcept>

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
			// writeReg no implementa SR; actualizar tambien el modo cacheado del JIT.
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
			require(dsp.getPC().toWord() == pc, "no se alcanza el PC esperado");
		}
	};

	void shortProgramMove(uint32_t blockSize)
	{
		Machine m(blockSize);
		// move #>$175,ssh; move ssh,p:<$17; move p:<$17,x0; jmp $105
		m.program(0x100, {0x05f43c, 0x175, 0x07173c, 0x079704, 0x0c0105, 0x0c0105});
		m.dsp.setPC(0x100);
		m.until(0x105);
		require(m.memory.get(dsp56k::MemArea_P, 0x17) == 0x175, "MOVEM no escribe P:$17");
		require(m.dsp.regs().x.var == 0x175, "MOVEM no lee P:$17");
		require(m.dsp.regs().sp.var == 0, "MOVEM SSH no equilibra la pila");
	}

	void invalidateProgramMove(uint32_t blockSize)
	{
		Machine m(blockSize);
		// Cachea primero el codigo que luego reescribe MOVEM.
		m.program(0x30, {0x241100, 0x0c0200}); // move #$11,x0; jmp $200
		m.program(0x200, {0x0c0200});
		m.dsp.setPC(0x30);
		m.until(0x200);
		const auto before = m.dsp.regs().x.var;
		// move #>$242200,x1; move x1,p:<$30; jmp $30
		m.program(0x100, {0x45f400, 0x242200, 0x073005, 0x0c0030});
		m.dsp.setPC(0x100);
		m.until(0x200);
		require(m.memory.get(dsp56k::MemArea_P, 0x30) == 0x242200, "MOVEM no reescribe el codigo");
		require((m.dsp.regs().x.var & 0xffffff) != (before & 0xffffff), "MOVEM deja codigo JIT obsoleto");
	}

	void foreverLoop(uint32_t blockSize, uint32_t lc)
	{
		Machine m(blockSize);
		// do forever,$104; inc a; nop; nop
		m.program(0x100, {0x000203, 0x104, 0x000008, 0, 0, 0x0c0105});
		m.dsp.regs().lc.var = lc;
		m.dsp.setPC(0x100);
		for(unsigned i = 0; i < 100; ++i) m.dsp.exec();
		require(m.dsp.getPC().toWord() >= 0x102 && m.dsp.getPC().toWord() <= 0x104, "DO FOREVER sale del bucle");
		require(m.dsp.regs().lc.var == lc, "DO FOREVER modifica LC");
		require(m.dsp.regs().sp.var == 2, "DO FOREVER no conserva su contexto en la pila");
		require(m.dsp.regs().a.var > 10, "DO FOREVER no repite el cuerpo");
		// Una IRQ larga debe volver al bucle, conservando sus dos entradas de pila.
		m.program(0x16, {0x0bf080, 0x180});
		m.program(0x180, {0x000009, 0x000004}); // inc b; rti
		m.dsp.injectInterrupt(0x16);
		for(unsigned i = 0; i < 30; ++i) m.dsp.exec();
		// B tiene desplazamiento interno; comprobar el registro arquitectonico B0.
		dsp56k::TReg24 b0;
		m.dsp.readReg(dsp56k::Reg_B0, b0);
		require(b0.var == 1, "IRQD no ejecuta el manejador");
		require(m.dsp.regs().sp.var == 2, "IRQD desequilibra DO FOREVER");
		require(m.dsp.getPC().toWord() >= 0x102 && m.dsp.getPC().toWord() <= 0x104, "RTI no vuelve a DO FOREVER");
	}

	void nestedLoop(uint32_t blockSize)
	{
		Machine m(blockSize);
		// DO FOREVER exterior, DO #2 interior. El fin del interior debe restaurar FV.
		m.program(0x100, {0x000203, 0x106, 0x060280, 0x104, 0x000008, 0x000009, 0, 0x0c0107});
		m.dsp.setPC(0x100);
		for(unsigned i = 0; i < 200; ++i) m.dsp.exec();
		require(m.dsp.regs().a.var > 10 && m.dsp.regs().b.var > 5, "el DO anidado no termina o pierde FV");
		require(m.dsp.regs().sp.var == 2 || m.dsp.regs().sp.var == 4, "DO anidado corrompe la pila");
		require(m.dsp.getPC().toWord() >= 0x102 && m.dsp.getPC().toWord() <= 0x106, "el DO exterior deja de repetir");
	}
}

int main()
{
	try
	{
		for(const auto blockSize : {1u, 32u})
		{
			shortProgramMove(blockSize);
			invalidateProgramMove(blockSize);
			foreverLoop(blockSize, 0);
			foreverLoop(blockSize, 7);
			nestedLoop(blockSize);
		}
		std::puts("OK: MOVEM corto, invalidacion JIT, DO FOREVER, IRQD y DO anidado (bloques 1/32)");
		return 0;
	}
	catch(const std::exception& error)
	{
		std::fprintf(stderr, "ERROR: %s\n", error.what());
		return 1;
	}
}
