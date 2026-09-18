#include "g1mc.h"

#include "mc68k/memoryOps.h"

#include <algorithm>

#define MC68K_CLASS g1::Microcontroller
#include "mc68k/musashiEntry.h"

namespace g1
{
	Microcontroller::Microcontroller(const std::vector<uint8_t>& _rom) : m_mem(g_memSize, 0)
	{
		std::copy_n(_rom.begin(), std::min<size_t>(_rom.size(), g_romSize), m_mem.begin());
		for(uint32_t i = 0; i < g_dspCount; ++i)
			m_dsps[i] = std::make_unique<Dsp>(m_hostPorts[i], i);
		reset();	// lee la pila y el PC de los vectores en $0 y $4
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
		return cycles;
	}

	void Microcontroller::catchUpDsps()
	{
		for(auto& dsp : m_dsps)
			dsp->catchUp(m_ucCycles * g_dspCyclesPerUcCycle);
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
			return Mc68k::read16(addr);
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
			return 0xff;	// ningun boton pulsado
		return 0;
	}

	uint8_t Microcontroller::read8(const uint32_t _addr)
	{
		const auto addr = _addr & 0xffffff;
		if(addr < g_memSize)
			return m_mem[addr];
		if(isInternalPeripheral(addr))
			return Mc68k::read8(addr);
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
			return 0xff;	// ningun boton pulsado
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
		logUnknown(addr, true, _val);
	}
}
