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
#include <array>
#include <functional>
#include <deque>
#include <map>
#include <memory>
#include <vector>

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
		uint64_t hostWords() const { return m_hostWords; }
		uint64_t irqdCount() const { return m_irqdCount; }
		const std::map<uint32_t, uint64_t>& servicedVectors() const { return m_servicedVectors; }
		uint32_t lastVector() const { return m_lastVector; }
		std::map<uint32_t, uint64_t>& pcWatch() { return m_pcWatch; }
		uint64_t hostCommands() const { return m_hostCommands; }
		uint64_t wordsToHost() const { return m_wordsToHost; }

		// Medidor: pico (valor absoluto, 24 bits con signo) por ESSI, slot y linea TX
		// desde el ultimo reset. Sirve para averiguar por donde sale el audio.
		static constexpr uint32_t MeterSlots = 4, MeterLines = 3;
		using Meter = std::array<std::array<std::array<uint32_t, MeterLines>, MeterSlots>, 2>;
		const Meter& meter() const { return m_meter; }
		void resetMeter() { m_meter = {}; }
		uint32_t lastSlotCount(uint32_t _essi) const { return m_slotCount[_essi]; }

		// Recibe cada trama que sale por un ESSI: (essi, slot 0 de TX0, slot 1 de TX0),
		// en 24 bits con signo. Sirve para grabar o reproducir la salida.
		using AudioCallback = std::function<void(uint32_t, int32_t, int32_t)>;
		void setAudioCallback(AudioCallback _cb) { m_audioCallback = std::move(_cb); }

		// Cadena de audio: lo que sale por los ESSI de este DSP entra por los del siguiente.
		void setNext(Dsp* _next) { m_next = _next; if(_next) _next->m_hasUpstream = true; }
		uint64_t chainedFrames() const { return m_chainedFrames; }

		// Recibe la salida de cada bloque (una muestra a 96 kHz): salidas 1/2 (ESSI0) y 3/4
		// (ESSI1), en 24 bits con signo. Con el programa de sonido del DSP 3, es lo que va al
		// codec. Se entrega en flushAudio.
		using BlockCallback = std::function<void(int32_t, int32_t, int32_t, int32_t)>;
		void setBlockCallback(BlockCallback _cb) { m_blockCallback = std::move(_cb); }

		// Ejecuta el DSP hasta llegar a _cycles ciclos (o hasta un tope si esta esperando).
		// Puede ir en su propio hilo: lo que sale por los ESSI se queda en una cola propia.
		void catchUp(uint64_t _cycles);

		// Reparte lo que ha salido por los ESSI desde la ultima vez: a la entrada del DSP
		// siguiente y al callback de audio. Solo desde el hilo de la CPU, con los DSP quietos.
		void flushAudio();

	private:
		void armBoot();
		void onBootFinished();
		void hostWord(uint32_t _word);
		void hostCommand(uint8_t _vector);
		uint8_t readIsr(uint8_t _isr);
		bool transferToHost();
		void runUntil(uint64_t _cycles);
		void drainAudio();
		bool irqdEnabled();
		void tapBlock();
		void tapLink(uint64_t _block);
		void readLink(uint32_t _essi, dsp56k::Audio::RxFrame& _frame);

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
		uint64_t m_hostWords = 0, m_hostCommands = 0, m_wordsToHost = 0;
		uint64_t m_nextIrqd = 0, m_irqdCount = 0;
		std::map<uint32_t, uint64_t> m_servicedVectors;
		uint32_t m_lastVector = 0;
		std::map<uint32_t, uint64_t> m_pcWatch;	// PCs a vigilar (solo diagnostico)
		Meter m_meter{};
		AudioCallback m_audioCallback;
		Dsp* m_next = nullptr;
		bool m_hasUpstream = false;
		struct LinkBlock { uint64_t index; std::array<dsp56k::TWord, 18> words; };
		std::vector<LinkBlock> m_linkOut;	// bloques de este DSP, pendientes de flushAudio
		std::deque<LinkBlock> m_linkIn;		// bloques del DSP anterior
		std::array<dsp56k::TWord, 2> m_craSeen{};
		BlockCallback m_blockCallback;
		using BlockFrame = std::array<dsp56k::TWord, 4>;
		std::vector<BlockFrame> m_blocks;	// pendiente de flushAudio
		struct StagedFrame { uint32_t slots; std::array<dsp56k::TWord, 4> v; };
		std::array<std::vector<StagedFrame>, 2> m_staged;	// por ESSI, pendiente de flushAudio
		uint64_t m_chainedFrames = 0;
		std::array<uint32_t, 2> m_slotCount{};
	};
}
