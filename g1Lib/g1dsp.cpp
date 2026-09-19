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

		// El G1 trabaja a 96 kHz con los DSP a 82,944 MHz: 864 ciclos por muestra.
		constexpr uint32_t g_samplerate = 96000;
		constexpr uint32_t g_dspClock = 82944000;
		constexpr uint32_t g_cyclesPerFrame = g_dspClock / g_samplerate;	// 864

		// Los ESSI van con el reloj que sale de su CRA (modo "fine link" del nucleo):
		// 2*(PM+1)*24 ciclos por palabra, 96 en los enlaces entre DSP (PM=1: 9 palabras por
		// muestra, justo lo que manda el DMA4 en cada bloque) y 144 en el DSP 3 hacia el
		// codec (PM=2). El reloj base solo sirve de tope: tiene que ser mas lento que ambos.
		constexpr uint32_t g_essiBaseCyclesPerWord = g_cyclesPerFrame / 2;

		// En modo asincrono el receptor usa el reloj del que le transmite (SC0 de fuera), no el
		// de su CRA. Todos los que transmiten por la cadena llevan PM=1: 96 ciclos por palabra.
		// El DSP 3 tiene PM=2 porque su CRA es para el codec: su receptor iria a 144 y solo
		// podria recoger 6 de las 9 palabras de cada muestra.
		constexpr uint32_t g_linkCyclesPerWord = 96;

		// Retardo del enlace entre DSP, en bloques. Los hilos se sincronizan cada ~4.000 ciclos
		// (unos 5 bloques): el que recibe coge el bloque de hace g_linkLatency, que seguro que
		// ya ha llegado. En el aparato es menos de un bloque; aqui son ~83 us por DSP.
		constexpr uint64_t g_linkLatency = 8;
		// El DMA de recepcion de cada ESSI escribe en un anillo de 9 palabras: X:$6C0 (ESSI0) y
		// X:$6C9 (ESSI1). La palabra i del bloque va siempre a base+i.
		constexpr dsp56k::TWord g_linkBase[2] = {0x6c0, 0x6c9};

		// Periodo de palabra que sale de un CRA (DSP56303UM, fig. 7-3), como el nucleo.
		uint32_t essiWordCycles(const dsp56k::TWord _cra)
		{
			static constexpr uint32_t bits[8] = {8, 12, 16, 24, 32, 32, 24, 24};
			const uint32_t pm = (_cra & 0xff) + 1;
			const uint32_t prescale = (_cra & (1u << 11)) ? 1 : 8;
			return 2 * pm * prescale * bits[(_cra >> 19) & 7];
		}

		// La patilla IRQD de cada DSP recibe el reloj de muestra; su vector ($16) salta a la
		// rutina de bloque ($175), que calcula una muestra del patch.
		constexpr dsp56k::TWord g_irqdVector = 0x16;

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
		clock.setSamplerate(g_samplerate * 2);
		clock.setCyclesPerSample(g_essiBaseCyclesPerWord);

		// Tiene que estar activo antes de que el programa escriba CRA. El receptor de un enlace
		// solo avanza cuando le ha llegado una palabra: si no, el DMA de recepcion cogeria
		// palabras inventadas y los canales del enlace se desplazarian.
		for(auto* essi : {&m_periph.getEssi0(), &m_periph.getEssi1()})
			essi->setFineLinkMode(true);

		// Enlace por posicion: cuando el receptor pide una trama, se mira a que palabra del
		// anillo va a escribir su DMA y se le da ese canal del bloque del DSP anterior. Asi el
		// canal i acaba siempre en base+i, como en el aparato, sin depender de la fase entre los
		// ESSI ni de cuando se reactivo la recepcion (el aparato lo consigue con el reloj comun).
		for(uint32_t e = 0; e < 2; ++e)
		{
			auto& essi = e == 0 ? m_periph.getEssi0() : m_periph.getEssi1();
			essi.setReadRxCallback([this, e](uint64_t&, dsp56k::Audio::RxFrame& _frame)
			{
				if(!m_hasUpstream)
				{
					// El primer DSP no tiene a nadie delante: silencio.
					_frame.resize(2);
					_frame[0][0] = _frame[1][0] = 0;
					return;
				}
				readLink(e, _frame);
			});
		}

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
			if(before >= m_nextIrqd)
			{
				// Rejilla fija (no "ahora + periodo"): la IRQD no deriva respecto al reloj de los
				// ESSI, que tambien cuenta ciclos exactos. Si se ha quedado muy atras (parada de
				// recarga, arranque), se vuelve a enganchar sin rafaga de interrupciones.
				// En multiplos de 864 ciclos: los cuatro DSP comparten la rejilla, como en el aparato
				// (van a la par en ciclos), y el numero de bloque vale para todos.
				m_nextIrqd = (before - m_nextIrqd > g_cyclesPerFrame * 4) ? (before / g_cyclesPerFrame + 1) * g_cyclesPerFrame : m_nextIrqd + g_cyclesPerFrame;
				if(irqdEnabled())
				{
					if(m_blockCallback)
						tapBlock();
					if(m_next)
						tapLink(before / g_cyclesPerFrame);
					m_dsp.injectInterrupt(g_irqdVector);	// como un periferico: no bloquea
					++m_irqdCount;
				}
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

	// La salida del bloque anterior, antes de que empiece el siguiente. La rutina de bloque
	// alterna sus bufferes ($6C0/$6E0) y deja en X:$5 y X:$6 los que acaba de mandar por el
	// DMA4 (ESSI0) y el DMA5 (ESSI1): dos palabras cada uno. Leerlos aqui da exactamente una
	// muestra por bloque, sin depender de como el ESSI la parte en tramas.
	// Lo que este DSP acaba de mandar por sus dos ESSI en el bloque anterior: 9 palabras por
	// ESSI desde X:$5 y X:$6 (mismos bufferes que tapBlock), para el DSP siguiente.
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
		m_linkOut.push_back(b);
	}

	void Dsp::tapBlock()
	{
		auto& mem = m_dsp.memory();
		const auto p0 = mem.get(dsp56k::MemArea_X, 5);
		const auto p1 = mem.get(dsp56k::MemArea_X, 6);
		if(p0 < 0x600 || p0 > 0x7fe || p1 < 0x600 || p1 > 0x7fe)
			return;	// todavia no corre el programa de sonido
		BlockFrame f;
		f[0] = mem.get(dsp56k::MemArea_Y, p0);
		f[1] = mem.get(dsp56k::MemArea_Y, p0 + 1);
		f[2] = mem.get(dsp56k::MemArea_Y, p1);
		f[3] = mem.get(dsp56k::MemArea_Y, p1 + 1);
		m_blocks.push_back(f);
	}

	void Dsp::readLink(const uint32_t _essi, dsp56k::Audio::RxFrame& _frame)
	{
		_frame.resize(2);
		const auto ddr = m_periph.getDMA().getDDR(2 + _essi);
		const bool inRing = ddr >= g_linkBase[_essi] && ddr < g_linkBase[_essi] + 9;
		// La trama se pide en el slot 0, pero el slot 1 entra 96 ciclos despues y puede caer
		// ya en el bloque siguiente (9 palabras por bloque, 2 por trama): cada palabra se toma
		// del bloque en el que se va a recibir.
		for(uint32_t s = 0; s < 2; ++s)
		{
			const auto want = (m_dsp.getCycles() + s * g_linkCyclesPerWord) / g_cyclesPerFrame;
			const auto block = want > g_linkLatency ? want - g_linkLatency : 0;
			// Se tiran los bloques que ya no hacen falta. Si falta el que toca (el anterior estaba
			// parado recargando, sin IRQD), silencio: repetir uno viejo dejaria un zumbido.
			while(m_linkIn.size() > 1 && m_linkIn[1].index <= block)
				m_linkIn.pop_front();
			const LinkBlock* src = (!m_linkIn.empty() && m_linkIn.front().index == block) ? &m_linkIn.front() : nullptr;
			const auto pos = (ddr - g_linkBase[_essi] + s) % 9;
			_frame[s][0] = (src && inRing) ? src->words[_essi * 9 + pos] : 0;
		}
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
			// El receptor de un DSP con otro delante va al ritmo del que le transmite.
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
			// Tope por si el siguiente no consume (parado): unos 100 ms.
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
