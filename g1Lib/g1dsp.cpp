#include "g1dsp.h"

#include "mc68k/hdi08.h"

#include <algorithm>
#include <type_traits>
#include <vector>

namespace g1
{
	namespace
	{
		// El DSP56303 tiene 4K de P y 2K+2K de X/Y internos. Se reserva de sobra por si
		// algun programa del G1 usa mas; la ROM de arranque vive en $FF0000.
		constexpr dsp56k::TWord g_pMemSize = 0x10000;
		constexpr dsp56k::TWord g_xyMemSize = 0x10000;
		constexpr dsp56k::TWord g_externalMemAddr = 0x8000;
		constexpr dsp56k::TWord g_bootRom = 0xff0000;

		// El G1 trabaja a 96 kHz. El DSP emulado va a 6 ciclos por ciclo de CPU (20,97 MHz),
		// o sea ~125,8 MHz. El reloj del ESSI cuenta palabras y cada trama lleva dos
		// (estereo): una palabra cada 655 ciclos da 96.000 tramas por segundo.
		constexpr uint32_t g_samplerate = 96000;
		constexpr uint32_t g_wordsPerFrame = 2;
		constexpr uint32_t g_dspClock = 6u * 20971520u;
		constexpr uint32_t g_cyclesPerSample = (g_dspClock / g_wordsPerFrame + g_samplerate / 2) / g_samplerate;

		// La patilla IRQD de cada DSP recibe el reloj de muestra: su rutina (vector $16)
		// solo cuenta, y el bucle principal procesa un bloque cada 4 cuentas.
		constexpr dsp56k::TWord g_irqdVector = 0x16;
		constexpr uint32_t g_cyclesPerFrame = g_cyclesPerSample * g_wordsPerFrame;

		// Tope de ciclos que se deja correr a un DSP en una sola espera de la CPU.
		constexpr uint64_t g_waitClamp = 200000;
		// ...y en una consulta de estado (ISR), mucho menos: unas pocas palabras de margen.
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

		// Memoria de programa llena de RTS: un salto a basura no compila cosas raras.
		for(dsp56k::TWord i = 0; i < m_memory.sizeP(); ++i)
		{
			m_memory.set(dsp56k::MemArea_P, i, 0x00000c);
			m_dsp.getJit().notifyProgramMemWrite(i);
		}

		auto& clock = m_periph.getEssiClock();
		clock.setClockSource(dsp56k::EsxiClock::ClockSource::Cycles);
		clock.setSamplerate(g_samplerate * g_wordsPerFrame);
		clock.setCyclesPerSample(g_cyclesPerSample);

		// Mascaras de slots de los ESSI (TSMA/TSMB/RSMA/RSMB) a su valor de reset: todos los
		// slots activos. El emulador las deja a 0, y los DSP de voz del G1 no las escriben
		// nunca (solo el DSP 3 las ajusta): con 0 no transmitian ni pedian datos al DMA.
		for(const dsp56k::TWord reg : {0xffffb4u, 0xffffb3u, 0xffffb2u, 0xffffb1u, 0xffffa4u, 0xffffa3u, 0xffffa2u, 0xffffa1u})
			m_periph.write(reg, 0xffffff);

		// Sin hilos, un ESSI que espera audio de entrada bloquea todo. En el aparato real
		// el codec siempre entrega tramas: aqui se rellenan vacias (ver drainAudio).
		m_periph.getEssi0().writeEmptyAudioIn(64);
		m_periph.getEssi1().writeEmptyAudioIn(64);

		hdi08().setRXRateLimit(0);
		// Sin arbitraje: los host commands del G1 son interrupciones rapidas (un solo movep
		// en el vector, sin JSR/RTI). El arbitraje de Gearmulator espera ver el RTI en la
		// pila para dar el comando por terminado y con ellas se quedaba "ocupado" para siempre.
		hdi08().setHostCommandArbitration(false);
		m_dsp.setInterruptServicedCallback([this](const dsp56k::TWord _vba)
		{
			++m_servicedVectors[_vba];
			m_lastVector = _vba;
		});

		// Banderas HF0/HF1 del ICR de la CPU hacia el HSR del DSP.
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

		armBoot();
	}

	// Vuelve al estado de la ROM de arranque: las siguientes palabras son longitud,
	// direccion y programa.
	void Dsp::armBoot()
	{
		m_booted = false;

		// Lo que la CPU ya habia mandado y el programa anterior no llego a leer espera en
		// el puerto: en el aparato lo lee la ROM de arranque, asi que se le entrega en orden.
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
			// `jmp $FF0000`: el programa vuelve a la ROM de arranque.
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
			if(before >= m_nextIrqd && irqdEnabled())
			{
				m_dsp.injectInterrupt(g_irqdVector);	// como un periferico: no bloquea
				m_nextIrqd = before + g_cyclesPerFrame;
				++m_irqdCount;
			}
			m_dsp.exec();
			const auto now = m_dsp.getCycles();
			if(now == before)	// DSP parado (WAIT/STOP o detenido): no insistir
			{
				++m_stalls;
				return;
			}
			if((now & 0x3ff) < now - before)	// cada ~1000 ciclos (menos de una trama)
				drainAudio();
		}
	}

	// IRQD solo cuenta si el programa la tiene habilitada en el IPRC (X:$FFFFFF, nivel IDL en
	// los bits 9-10; 0 = deshabilitada) y la mascara del SR la deja pasar. El emulador solo
	// mira el SR: durante la parada de recarga (IPRC=$FF0800) el G1 la deshabilita a proposito.
	bool Dsp::irqdEnabled()
	{
		const auto iprc = m_periph.read(0xffffff, dsp56k::Instruction::Invalid);
		return ((iprc >> 9) & 3) != 0 && !m_dsp.isInterruptMasked(g_irqdVector);
	}

	void Dsp::catchUp(const uint64_t _cycles)
	{
		runUntil(_cycles);
		drainAudio();
		transferToHost();
	}

	// Saca lo que ha salido por los ESSI (a la cola de flushAudio) y rellena con silencio la
	// entrada si no llega nada, para que el ESSI no se quede esperando. Se miden los picos.
	void Dsp::drainAudio()
	{
		uint32_t e = 0;
		for(auto* essi : {&m_periph.getEssi0(), &m_periph.getEssi1()})
		{
			// Si no llega nada por la cadena (arranque, o el primer DSP), silencio.
			if(essi->getAudioInputs().size() < 16)
				essi->writeEmptyAudioIn(16);
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
							auto v = static_cast<int32_t>(_frame[s][l] << 8) >> 8;	// 24 bits con signo
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
		// Los dos ESSI van intercalados, trama a trama, como salian del DSP.
		const auto n = std::max(m_staged[0].size(), m_staged[1].size());
		for(size_t k = 0; k < n; ++k)
		{
			for(uint32_t e = 0; e < 2; ++e)
			{
				if(k >= m_staged[e].size())
					continue;
				const auto& f = m_staged[e][k];
				if(m_next)
				{
					auto& in = (e == 0 ? m_next->m_periph.getEssi0() : m_next->m_periph.getEssi1()).getAudioInputs();
					if(!in.full())
					{
						dsp56k::Audio::RxFrame rx;
						rx.resize(f.slots);
						for(uint32_t s = 0; s < f.slots; ++s)
							rx[s][0] = f.v[s];
						in.push_back(rx);
						++m_chainedFrames;
					}
				}
				if(m_audioCallback && f.slots >= 2)
					m_audioCallback(e, static_cast<int32_t>(f.v[0] << 8) >> 8, static_cast<int32_t>(f.v[1] << 8) >> 8);
			}
		}
		m_staged[0].clear();
		m_staged[1].clear();
	}

	void Dsp::hostWord(const uint32_t _word)
	{
		// HRX guarda una sola palabra: si la anterior sigue ahi, se deja correr al DSP.
		const auto stop = m_dsp.getCycles() + g_waitClamp;
		while(hdi08().hasRXData() && m_booted && m_dsp.getCycles() < stop)
			runUntil(m_dsp.getCycles() + 64);
		// Si mientras tanto el programa ha vuelto a la ROM de arranque, la palabra es suya.
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
		// El DSP real atiende cada host command en cuanto llega. Aqui, en un solo hilo, se le
		// deja correr hasta despachar lo pendiente: si no, la cola de interrupciones externas
		// (32 entradas) se llena e injectExternalInterrupt espera para siempre.
		const auto stop = m_dsp.getCycles() + g_waitClamp;
		while(m_dsp.hasPendingInterrupts() && m_booted && m_dsp.getCycles() < stop)
			runUntil(m_dsp.getCycles() + 16);
		if(m_dsp.hasPendingInterrupts())
			return;	// el DSP no las atiende (parado): mejor perder el comando que colgarse
		hdi08().writeHostCommand(_vector);
		++m_hostCommands;
		transferToHost();
	}

	uint8_t Dsp::readIsr(uint8_t _isr)
	{
		// En el aparato el DSP recoge enseguida la palabra que le llega; aqui puede ir
		// por detras. Si la CPU pregunta con una palabra pendiente, se deja correr al DSP
		// hasta que la recoja: la rutina del OS descarta la palabra tras 10 consultas.
		// Espera corta: si el DSP esta en un bucle que no lee el puerto (por ejemplo parado
		// con HF2 esperando a que la CPU baje HF0), la consulta devuelve el estado tal cual.
		if(m_booted && hdi08().hasRXData())
		{
			const auto stop = m_dsp.getCycles() + g_isrWaitClamp;
			while(hdi08().hasRXData() && m_booted && m_dsp.getCycles() < stop)
				runUntil(m_dsp.getCycles() + 64);
		}
		transferToHost();
		_isr = static_cast<uint8_t>((_isr & ~mc68k::Hdi08::Rxdf) | (m_hdiUc.canReceiveData() ? 0 : mc68k::Hdi08::Rxdf));

		// HF2/HF3 del DSP hacia el ISR de la CPU.
		const auto hf23 = hdi08().readControlRegister() & 0x18;
		_isr = static_cast<uint8_t>((_isr & ~0x18) | hf23);

		// TXDE: hay sitio para otra palabra. Durante el arranque, la ROM siempre acepta.
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
