#include "g1mc.h"

#include "mc68k/memoryOps.h"

#include <algorithm>
#include <cstdlib>

// The spin hint the worker threads use while they wait for the next block. __builtin_ia32_pause
// is GCC and Clang's; MSVC has no such builtin, and _M_X64 is defined there too, so guarding by
// architecture alone was enough to break the Windows build.
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#endif

namespace
{
	inline void cpuPause()
	{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
		_mm_pause();
#elif defined(_M_ARM64)
		__yield();
#elif defined(__aarch64__) || defined(__arm__)
		__asm__ __volatile__("yield");
#endif
	}
}

#define MC68K_CLASS g1::Microcontroller
#include "mc68k/musashiEntry.h"

namespace g1
{
	Microcontroller::Microcontroller(const std::vector<uint8_t>& _rom) : m_mem(g_memSize, 0), m_sci(getQSM(), static_cast<float>(g_sciRate))
	{
		std::copy_n(_rom.begin(), std::min<size_t>(_rom.size(), g_romSize), m_mem.begin());
		for(uint32_t i = 0; i < g_dspCount; ++i)
			m_dsps[i] = std::make_unique<Dsp>(m_hostPorts[i], i);
		// Audio chain through the ESSIs: DSP0 -> DSP1 -> DSP2 -> DSP3 -> codec. Each DSP copies what
		// it receives into its output buffer (DMA0) and adds its voices; DSP 3 applies the volume.
		for(uint32_t i = 0; i + 1 < g_dspCount; ++i)
			m_dsps[i]->setNext(m_dsps[i + 1].get());

		// The OS reads the master volume from the ADC and sets DSP 3's gain (Y:$5F) with it.
		// At zero, nothing comes out. The other knobs stay at zero.
		m_adc[g_adcVolume] = 0xff;

		// DUART bus: port E carries /CS, /RD, /WR and the register address.
		getPortE().setWriteTXCallback([this](const mc68k::Port& _port) { onPortE(_port.read()); });
		reset();	// reads the stack and the PC from the vectors at $0 and $4

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
			// Syncs are very frequent (~20,000 per second): the threads spin while waiting and only
			// sleep if the CPU takes long (for instance when the emulator has time to spare).
			uint32_t spins = 0;
			while(m_generation.load() == seen && !m_quitWorkers)
			{
				if(++spins < 20000)
				{
					cpuPause();
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
		const uint32_t len = (g_romOsLongs - 1) * 4;	// the loader copies (len >> 2) + 1 long words
		for(int i = 0; i < 4; ++i)	// big endian, as the 68k reads it
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
		if((m_ucCycles & 0x3ff) < cycles)	// every ~1000 CPU cycles
		{
			const auto pos = m_pcTrailPos.load(std::memory_order_relaxed);
			m_pcTrail[pos].store(getPC(), std::memory_order_relaxed);
			m_pcTrailPos.store((pos + 1) % g_pcTrail, std::memory_order_relaxed);
		}
		if((m_ucCycles & 0x3ff) < cycles)
			catchUpDsps();
		execPcPort();
		execPit(cycles);
		while(m_ucCycles >= m_nextSciSample)	// the UART advances at its clock rate
		{
			m_sci.process(1);
			m_nextSciSample += g_ucCyclesPerSciSample;
		}
		return cycles;
	}

	// Falling edge of /RD or /WR with /CS active = access to a DUART register.
	void Microcontroller::onPortE(const uint8_t _value)
	{
		const auto prev = m_prevPortE;
		m_prevPortE = _value;
		if(_value & 0x01)	// /CS inactive
			return;
		const auto reg = static_cast<uint8_t>(((_value >> 3) & 1) | (((_value >> 6) & 1) << 1) | (((_value >> 7) & 1) << 2));
		const bool rd = (prev & 0x02) && !(_value & 0x02);
		const bool wr = (prev & 0x04) && !(_value & 0x04);
		if(rd)
			getPortGP().writeRX(m_pcPort.read(reg));
		else if(wr)
			m_pcPort.write(reg, getPortGP().read());
	}

	// The DUART's RxRDY goes to the PAI pin. The OS leaves PACNT at $FF with the overflow
	// interrupt (PAOVI, TMSK2 bit 5) enabled: the first pulse fires it. The real 68331 does it
	// in its GPT; Gearmulator's does not emulate the accumulator, so it is done here.
	void Microcontroller::execPcPort()
	{
		if(m_ucCycles < m_nextPcPortByte || !m_pcPort.hasRx())
			return;
		const auto tmsk2 = Mc68k::read8(0xfff921);
		const auto tflg2 = Mc68k::read8(0xfff923);
		if(!(tmsk2 & 0x20) || (tflg2 & 0x20))
			return;
		Mc68k::write8(0xfff90d, 0x00);						// PACNT overflows
		Mc68k::write8(0xfff923, static_cast<uint8_t>(tflg2 | 0x20));	// PAOVF
		getGPT().injectInterrupt(0xa);						// PAOV: the OS puts its handler at IVBA+$A
		++m_pcPortIrqs;
		m_nextPcPortByte = m_ucCycles + g_ucCyclesPerSerialByte;
	}

	// The 68331 SIM's PIT (Gearmulator's SIM does not emulate it). PICR ($FFFA22): level in
	// bits 10-8 and vector in 7-0. PITR ($FFFA24): modulus in 7-0 and the /512 prescaler in
	// bit 8. Period = PITM * 4 (* 512) / 32768 s. The OS uses it as its system clock
	// ($1008A4, every 244 us): without it, its software timers never expire.
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
				cpuPause();
			}
		}
		// With all DSPs stopped, the audio goes from each one to the next.
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
		const auto addr = _addr & 0xffffff;	// the 68331 has 24 address lines
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
			// Each read returns the previous conversion and starts a new one on the selected channel.
			// That is how the OS uses it: at boot it selects and reads twice (the second is the good one);
			// at runtime it selects the next channel, reads, and stores the value in the previous one ($1041BE).
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

	// The buttons of the row selected in $202005 (bits 4-6, active low). Pressed = 0.
	// Bits 0 and 1 are not buttons: they are the dial, which is not multiplexed and so
	// reads the same whichever row is selected.
	uint8_t Microcontroller::buttonRow() const
	{
		uint8_t v = 0xff;
		for(uint32_t row = 0; row < 3; ++row)
			if(!(m_panelRows & (0x10u << row)))
				v &= static_cast<uint8_t>(~m_buttons[row].load(std::memory_order_relaxed));
		return static_cast<uint8_t>((v & 0xfc) | dialBits());
	}

	// The dial's two phases, one edge at a time: 00, 10, 11, 01 clockwise (which is how the
	// OS counts up) and the other way round anticlockwise.
	uint8_t Microcontroller::dialBits() const
	{
		constexpr uint8_t gray[4] = {0, 2, 3, 1};
		const auto pending = m_dialEdges.load(std::memory_order_relaxed);
		if(pending && m_ucCycles >= m_nextDialEdge)
		{
			const int32_t dir = pending > 0 ? 1 : -1;
			m_dialPhase = static_cast<uint8_t>((m_dialPhase + 4 + dir) & 3);
			m_dialEdges.fetch_sub(dir, std::memory_order_relaxed);
			m_nextDialEdge = m_ucCycles + g_dialEdgeCycles;
		}
		return gray[m_dialPhase];
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
			m_adcSelect = _val;	// ADC multiplexer channel
		else if(addr == g_panelLeds)
		{
			m_ledLatch = _val;
			return;
		}
		else if(addr == g_panelRows)
		{
			// Low nibble: the LED row lit with whatever was last put in $202004
			// (bit 3 = row 0 ... bit 0 = row 3). Bits 4-6: button row, active low.
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
