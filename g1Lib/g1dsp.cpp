#include "g1dsp.h"

#include "mc68k/hdi08.h"

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

		// Tope de ciclos que se deja correr a un DSP en una sola espera de la CPU.
		constexpr uint64_t g_waitClamp = 200000;
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
		m_dsp.getJit().setConfig(config);

		// Memoria de programa llena de RTS: un salto a basura no compila cosas raras.
		for(dsp56k::TWord i = 0; i < m_memory.sizeP(); ++i)
		{
			m_memory.set(dsp56k::MemArea_P, i, 0x00000c);
			m_dsp.getJit().notifyProgramMemWrite(i);
		}

		// Sin hilos, un ESSI que espera audio de entrada bloquea todo. En el aparato real
		// el codec siempre entrega tramas: aqui se rellenan vacias (ver drainAudio).
		m_periph.getEssi0().writeEmptyAudioIn(64);
		m_periph.getEssi1().writeEmptyAudioIn(64);

		hdi08().setRXRateLimit(0);
		hdi08().setHostCommandArbitration(true);

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
		m_boot = std::make_unique<dsp56k::DspBoot>(m_dsp);
		m_hdiUc.setWriteTxCallback([this](const uint32_t _word)
		{
			if(m_boot->hdiWriteTX(_word))
				onBootFinished();
		});
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
			const auto before = m_dsp.getCycles();
			m_dsp.exec();
			const auto now = m_dsp.getCycles();
			if(now == before)	// DSP parado (WAIT/STOP o detenido): no insistir
			{
				++m_stalls;
				return;
			}
			if((now & 0xfff) < now - before)	// cada ~4000 ciclos
				drainAudio();
		}
	}

	void Dsp::catchUp(const uint64_t _cycles)
	{
		runUntil(_cycles);
		drainAudio();
		transferToHost();
	}

	// Todavia no se escucha: se descarta lo que sale por los ESSI para que su cola no
	// se llene y bloquee al DSP, y se rellena la entrada con silencio. Se cuentan las
	// tramas para saber si el DSP esta generando audio.
	void Dsp::drainAudio()
	{
		for(auto* essi : {&m_periph.getEssi0(), &m_periph.getEssi1()})
		{
			if(essi->getAudioInputs().size() < 32)
				essi->writeEmptyAudioIn(64);
			auto& out = essi->getAudioOutputs();
			while(!out.empty())
			{
				out.pop_front();
				++m_audioFrames;
			}
		}
	}

	void Dsp::hostWord(const uint32_t _word)
	{
		// HRX guarda una sola palabra: si la anterior sigue ahi, se deja correr al DSP.
		const auto stop = m_dsp.getCycles() + g_waitClamp;
		while(hdi08().hasRXData() && m_booted && m_dsp.getCycles() < stop)
			runUntil(m_dsp.getCycles() + 64);
		hdi08().writeRX(&_word, 1);
	}

	void Dsp::hostCommand(const uint8_t _vector)
	{
		if(!m_booted)
			return;
		// Un host command no puede pisar al anterior mientras su rutina no haya terminado.
		const auto stop = m_dsp.getCycles() + g_waitClamp;
		while(hdi08().hostCommandBusy() && m_booted && m_dsp.getCycles() < stop)
			runUntil(m_dsp.getCycles() + 64);
		hdi08().writeHostCommand(_vector);
		transferToHost();
	}

	uint8_t Dsp::readIsr(uint8_t _isr)
	{
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
			return true;
		}
		return false;
	}
}
