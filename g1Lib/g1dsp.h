#pragma once

// Un DSP56303 del G1, emulado con dsp56kEmu, conectado al registro HI08 que ve la CPU.
//
// El G1 arranca los DSP por el puerto host (HI08): la ROM de arranque del DSP recibe
// longitud, direccion y palabras, y salta al programa. El DSP 3 recibe primero un
// programa corto del cargador (PLL, puertos serie, codec) que acaba con `jmp $FF0000`,
// es decir, vuelve a su ROM de arranque para recibir despues el programa de sonido.
// Aqui se detecta ese salto y se rearma el arranque.
//
// Sin hilos: la CPU manda y cada DSP se pone al dia (catchUp) cuando la CPU lo mira o
// le escribe, y periodicamente desde el bucle principal.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/dspBootCode.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

#include <cstdint>
#include <memory>

namespace mc68k { class Hdi08; }

namespace g1
{
	class Dsp
	{
	public:
		Dsp(mc68k::Hdi08& _hdiUc, uint32_t _index);

		dsp56k::DSP& dsp() { return m_dsp; }
		dsp56k::HDI08& hdi08() { return m_periph.getHI08(); }
		dsp56k::Peripherals56303& periph() { return m_periph; }

		bool booted() const { return m_booted; }
		uint32_t bootCount() const { return m_bootCount; }
		uint32_t index() const { return m_index; }
		uint64_t stalls() const { return m_stalls; }
		uint64_t audioFrames() const { return m_audioFrames; }

		// Ejecuta el DSP hasta llegar a _cycles ciclos (o hasta un tope si esta esperando).
		void catchUp(uint64_t _cycles);

	private:
		void armBoot();
		void onBootFinished();
		void hostWord(uint32_t _word);
		void hostCommand(uint8_t _vector);
		uint8_t readIsr(uint8_t _isr);
		bool transferToHost();
		void runUntil(uint64_t _cycles);
		void drainAudio();

		mc68k::Hdi08& m_hdiUc;
		const uint32_t m_index;

		dsp56k::DefaultMemoryValidator m_validator;
		dsp56k::PeripheralsNop m_periphNop;
		dsp56k::Peripherals56303 m_periph;
		dsp56k::Memory m_memory;
		dsp56k::DSP m_dsp;
		std::unique_ptr<dsp56k::DspBoot> m_boot;

		bool m_booted = false;
		uint32_t m_bootCount = 0;
		uint64_t m_stalls = 0;
		uint64_t m_audioFrames = 0;
	};
}
