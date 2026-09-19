#include "g1mc.h"

#include "mc68k/memoryOps.h"

#include <algorithm>
#include <cstdlib>

#define MC68K_CLASS g1::Microcontroller
#include "mc68k/musashiEntry.h"

namespace g1
{
	Microcontroller::Microcontroller(const std::vector<uint8_t>& _rom) : m_mem(g_memSize, 0), m_sci(getQSM(), static_cast<float>(g_sciRate))
	{
		std::copy_n(_rom.begin(), std::min<size_t>(_rom.size(), g_romSize), m_mem.begin());
		for(uint32_t i = 0; i < g_dspCount; ++i)
			m_dsps[i] = std::make_unique<Dsp>(m_hostPorts[i], i);
		// Cadena de audio por los ESSI: DSP0 -> DSP1 -> DSP2 -> DSP3 -> codec. Cada DSP copia lo
		// que recibe a su bufer de salida (DMA0) y le suma sus voces; el DSP 3 aplica el volumen.
		for(uint32_t i = 0; i + 1 < g_dspCount; ++i)
			m_dsps[i]->setNext(m_dsps[i + 1].get());

		// El OS lee el volumen maestro del ADC al encender y con el pone la ganancia del DSP 3
		// (Y:$5F). A cero, no sale nada. Los demas mandos se quedan a cero.
		m_adc[g_adcVolume] = 0xff;

		// Bus del DUART: el puerto E lleva /CS, /RD, /WR y la direccion del registro.
		getPortE().setWriteTXCallback([this](const mc68k::Port& _port) { onPortE(_port.read()); });
		reset();	// lee la pila y el PC de los vectores en $0 y $4

		if(const char* t = std::getenv("G1_THREADS"))
			m_threaded = std::atoi(t) != 0;
		if(m_threaded)
			for(uint32_t i = 1; i < g_dspCount; ++i)
				m_workers.emplace_back([this, i] { workerLoop(i); });
	}

	Microcontroller::~Microcontroller()
	{
		m_quitWorkers = true;
		{
			std::lock_guard lock(m_wakeMutex);
			m_wake.notify_all();
		}
		for(auto& t : m_workers)
			t.join();
	}

	void Microcontroller::workerLoop(const uint32_t _dsp)
	{
		uint64_t seen = 0;
		while(true)
		{
			// Las sincronizaciones son muy seguidas (~20.000 por segundo): se espera girando y
			// solo se duerme si la CPU tarda (por ejemplo, cuando el emulador va sobrado).
			uint32_t spins = 0;
			while(m_generation.load() == seen && !m_quitWorkers)
			{
				if(++spins < 20000)
				{
#if defined(__x86_64__) || defined(_M_X64)
					__builtin_ia32_pause();
#endif
					continue;
				}
				std::unique_lock lock(m_wakeMutex);
				++m_sleepers;
				m_wake.wait(lock, [&] { return m_generation.load() != seen || m_quitWorkers; });
				--m_sleepers;
			}
			if(m_quitWorkers)
				return;
			seen = m_generation.load();
			m_dsps[_dsp]->catchUp(m_dspTarget);
			m_pending.fetch_sub(1);
		}
	}

	void Microcontroller::installRomOsInFlash()
	{
		auto& flash = m_flash.data();
		std::fill(flash.begin(), flash.end(), 0xff);
		const uint32_t bytes = g_romOsLongs * 4;
		const uint32_t len = (g_romOsLongs - 1) * 4;	// el cargador copia (len >> 2) + 1 palabras largas
		for(int i = 0; i < 4; ++i)	// big endian, como lo lee el 68k
			flash[8 + static_cast<size_t>(i)] = static_cast<uint8_t>(len >> (24 - 8 * i));
		std::copy_n(m_mem.begin() + g_romOsOffset, std::min<uint32_t>(bytes, g_romSize - g_romOsOffset), flash.begin() + 0x20);
	}

	uint16_t Microcontroller::readImm16(const uint32_t _addr)
	{
		return mc68k::memoryOps::readU16(m_mem.data(), _addr & (g_memSize - 1));
	}

	uint32_t Microcontroller::exec()
	{
		const auto cycles = Mc68k::exec();
		m_ucCycles += cycles;
		for(auto& port : m_hostPorts)
			port.exec(cycles);
		if((m_ucCycles & 0x3ff) < cycles)	// cada ~1000 ciclos de CPU
			catchUpDsps();
		execPcPort();
		execPit(cycles);
		while(m_ucCycles >= m_nextSciSample)	// la UART avanza al ritmo de su reloj
		{
			m_sci.process(1);
			m_nextSciSample += g_ucCyclesPerSciSample;
		}
		return cycles;
	}

	// Flanco de bajada de /RD o /WR con /CS activo = acceso a un registro del DUART.
	void Microcontroller::onPortE(const uint8_t _value)
	{
		const auto prev = m_prevPortE;
		m_prevPortE = _value;
		if(_value & 0x01)	// /CS inactivo
			return;
		const auto reg = static_cast<uint8_t>(((_value >> 3) & 1) | (((_value >> 6) & 1) << 1) | (((_value >> 7) & 1) << 2));
		const bool rd = (prev & 0x02) && !(_value & 0x02);
		const bool wr = (prev & 0x04) && !(_value & 0x04);
		if(rd)
			getPortGP().writeRX(m_pcPort.read(reg));
		else if(wr)
			m_pcPort.write(reg, getPortGP().read());
	}

	// RxRDY del DUART va a la patilla PAI. El OS deja PACNT en $FF con la interrupcion de
	// desbordamiento (PAOVI, bit 5 de TMSK2) activa: el primer pulso la dispara. El 68331
	// real lo hace en su GPT; el de Gearmulator no emula el acumulador, asi que va aqui.
	void Microcontroller::execPcPort()
	{
		if(m_ucCycles < m_nextPcPortByte || !m_pcPort.hasRx())
			return;
		const auto tmsk2 = Mc68k::read8(0xfff921);
		const auto tflg2 = Mc68k::read8(0xfff923);
		if(!(tmsk2 & 0x20) || (tflg2 & 0x20))
			return;
		Mc68k::write8(0xfff90d, 0x00);						// PACNT desborda
		Mc68k::write8(0xfff923, static_cast<uint8_t>(tflg2 | 0x20));	// PAOVF
		getGPT().injectInterrupt(0xa);						// PAOV: el OS pone su manejador en IVBA+$A
		++m_pcPortIrqs;
		m_nextPcPortByte = m_ucCycles + g_ucCyclesPerSerialByte;
	}

	// PIT del SIM del 68331 (el SIM de Gearmulator no lo emula). PICR ($FFFA22): nivel en
	// los bits 10-8 y vector en 7-0. PITR ($FFFA24): modulo en 7-0 y prescaler /512 en el
	// bit 8. Periodo = PITM * 4 (* 512) / 32768 s. El OS lo usa como reloj del sistema
	// ($1008A4, cada 244 us): sin el, sus temporizadores por software no vencen nunca.
	void Microcontroller::execPit(const uint32_t _cycles)
	{
		const uint32_t pitm = m_pitr & 0xff;
		const uint32_t level = (m_picr >> 8) & 7;
		if(!pitm || !level)
			return;
		const uint64_t period = static_cast<uint64_t>(pitm) * 4 * ((m_pitr & 0x100) ? 512 : 1) * g_ucClock / 32768;
		m_pitAccum += _cycles;
		if(m_pitAccum < period)
			return;
		m_pitAccum -= period;
		const auto vector = static_cast<uint8_t>(m_picr & 0xff);
		if(!hasPendingInterrupt(vector, static_cast<uint8_t>(level)))
			injectInterrupt(vector, static_cast<uint8_t>(level));
		++m_pitIrqs;
	}

	uint32_t Microcontroller::getSR() const
	{
		return m68k_get_reg(const_cast<Microcontroller*>(this)->getCpuState(), M68K_REG_SR);
	}

	void Microcontroller::catchUpDsps()
	{
		const auto target = m_ucCycles * g_dspCyclesPerUcNum / g_dspCyclesPerUcDen;
		if(!m_threaded)
		{
			for(auto& dsp : m_dsps)
				dsp->catchUp(target);
		}
		else
		{
			m_dspTarget = target;
			m_pending.store(static_cast<uint32_t>(m_workers.size()));
			m_generation.fetch_add(1);
			if(m_sleepers.load() > 0)
			{
				std::lock_guard lock(m_wakeMutex);
				m_wake.notify_all();
			}
			m_dsps[0]->catchUp(target);
			while(m_pending.load() != 0)
			{
#if defined(__x86_64__) || defined(_M_X64)
				__builtin_ia32_pause();
#endif
			}
		}
		// Con todos los DSP quietos, el audio pasa de cada uno al siguiente.
		for(auto& dsp : m_dsps)
			dsp->flushAudio();
	}

	void Microcontroller::traceHost(const uint32_t _addr, const bool _write, const uint32_t _value)
	{
		if(m_hostTrace.size() < 200000)
			m_hostTrace.push_back({_addr, _value, getPC(), _write, 0});
	}

	void Microcontroller::logUnknown(const uint32_t _addr, const bool _write, const uint32_t _value)
	{
		auto& a = m_unknown[_addr];
		if(!a.reads && !a.writes)
			a.firstPc = getPC();
		(_write ? a.writes : a.reads)++;
		a.lastValue = _value;
	}

	uint16_t Microcontroller::read16(const uint32_t _addr)
	{
		const auto addr = _addr & 0xffffff;	// el 68331 tiene 24 lineas de direccion
		if(addr < g_memSize)
			return mc68k::memoryOps::readU16(m_mem.data(), addr);
		if(isInternalPeripheral(addr))
		{
			if(addr == 0xfffc0e) ++m_sciDataReads;
			return Mc68k::read16(addr);
		}
		if(isHostPort(addr))
		{
			traceHost(addr, false, 0);
			catchUpDsps();
			return hostPort(addr).read16(hostReg(addr));
		}
		if(addr >= g_flashAddress && addr < g_flashAddress + g_flashSize - 1)
			return static_cast<uint16_t>((m_flash.read(addr - g_flashAddress) << 8) | m_flash.read(addr - g_flashAddress + 1));
		logUnknown(addr, false, 0);
		if(addr == g_panelIn)
			return buttonRow();
		return 0;
	}

	uint8_t Microcontroller::read8(const uint32_t _addr)
	{
		const auto addr = _addr & 0xffffff;
		if(addr < g_memSize)
			return m_mem[addr];
		if(isInternalPeripheral(addr))
		{
			if(addr == 0xfffc0e || addr == 0xfffc0f) ++m_sciDataReads;
			return Mc68k::read8(addr);
		}
		if(isHostPort(addr))
		{
			traceHost(addr, false, 0);
			catchUpDsps();
			return hostPort(addr).read8(hostReg(addr));
		}
		if(addr >= g_flashAddress && addr < g_flashAddress + g_flashSize)
			return m_flash.read(addr - g_flashAddress);
		logUnknown(addr, false, 0);
		if(addr == g_panelIn)
			return buttonRow();
		if(addr == g_panelAdc)
		{
			// Cada lectura devuelve la conversion anterior y arranca otra con el canal elegido. Asi
			// lo usa el OS: al encender elige y lee dos veces (la segunda es la buena); en marcha
			// elige el canal siguiente y lee, y guarda lo leido en el anterior ($1041BE).
			const auto v = m_adcResult;
			m_adcResult = m_adc[m_adcSelect];
			return v;
		}
		return 0;
	}

	void Microcontroller::write16(const uint32_t _addr, const uint16_t _val)
	{
		const auto addr = _addr & 0xffffff;
		if(addr < g_romSize)
		{
			++m_romWrites;
			logUnknown(addr, true, _val);
			return;
		}
		if(addr < g_memSize)
		{
			mc68k::memoryOps::writeU16(m_mem.data(), addr, _val);
			return;
		}
		if(isInternalPeripheral(addr))
		{
			if(addr == 0xfffc0e) ++m_sciDataWrites;
			if(addr == 0xfffa22) m_picr = _val;
			if(addr == 0xfffa24) m_pitr = _val;
			Mc68k::write16(addr, _val);
			return;
		}
		if(isHostPort(addr))
		{
			traceHost(addr, true, _val);
			catchUpDsps();
			hostPort(addr).write16(hostReg(addr), _val);
			return;
		}
		if(addr >= g_flashAddress && addr < g_flashAddress + g_flashSize - 1)
		{
			m_flash.write(addr - g_flashAddress, static_cast<uint8_t>(_val >> 8));
			m_flash.write(addr - g_flashAddress + 1, static_cast<uint8_t>(_val));
			return;
		}
		logUnknown(addr, true, _val);
	}

	// Los botones de la fila elegida en $202005 (bits 4-6, a nivel bajo). Pulsado = 0.
	uint8_t Microcontroller::buttonRow() const
	{
		uint8_t v = 0xff;
		for(uint32_t row = 0; row < 3; ++row)
			if(!(m_panelRows & (0x10u << row)))
				v &= static_cast<uint8_t>(~m_buttons[row].load(std::memory_order_relaxed));
		return v;
	}

	void Microcontroller::write8(const uint32_t _addr, const uint8_t _val)
	{
		const auto addr = _addr & 0xffffff;
		if(addr < g_romSize)
		{
			++m_romWrites;
			logUnknown(addr, true, _val);
			return;
		}
		if(addr < g_memSize)
		{
			m_mem[addr] = _val;
			return;
		}
		if(isInternalPeripheral(addr))
		{
			if(addr == 0xfffc0e || addr == 0xfffc0f) ++m_sciDataWrites;
			Mc68k::write8(addr, _val);
			return;
		}
		if(isHostPort(addr))
		{
			traceHost(addr, true, _val);
			catchUpDsps();
			hostPort(addr).write8(hostReg(addr), _val);
			return;
		}
		if(addr >= g_flashAddress && addr < g_flashAddress + g_flashSize)
		{
			m_flash.write(addr - g_flashAddress, _val);
			return;
		}
		if(addr == g_panelOut)
			m_adcSelect = _val;	// canal del multiplexor del ADC
		else if(addr == g_panelLeds)
		{
			m_ledLatch = _val;
			return;
		}
		else if(addr == g_panelRows)
		{
			// Nibble bajo: la fila de LEDs que se enciende con lo ultimo que se puso en $202004
			// (bit 3 = fila 0 ... bit 0 = fila 3). Bits 4-6: fila de botones, a nivel bajo.
			m_panelRows = _val;
			for(uint32_t row = 0; row < 4; ++row)
				if(_val & (8u >> row))
					m_leds[row].store(m_ledLatch, std::memory_order_relaxed);
			return;
		}
		else if(addr == g_lcdData)
		{
			m_lcd.writeData(_val);
			return;
		}
		else if(addr == g_lcdControl)
		{
			m_lcd.writeControl(_val);
			return;
		}
		logUnknown(addr, true, _val);
	}
}
