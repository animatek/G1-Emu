#pragma once

// The panel display: a character LCD with an HD44780 controller on an 8-bit bus.
// The CPU puts the byte in $202006 and moves the control lines in $202007: bit 0 = RS
// (0 command, 1 character) and bit 1 = E. The controller latches it when E falls. The OS
// initialises it with $30 three times and $38 (8 bits, two lines) and never reads the busy flag.

#include <array>
#include <cstdint>
#include <mutex>
#include <string>

namespace g1
{
	class Lcd
	{
	public:
		static constexpr uint32_t Columns = 40;	// HD44780 memory per line (fewer are visible)

		void writeData(const uint8_t _v) { m_bus = _v; }

		void writeControl(const uint8_t _v)
		{
			const bool e = (_v & 2) != 0;
			if(m_e && !e)
				latch((m_control & 1) != 0, m_bus);
			m_e = e;
			m_control = _v;
		}

		// Text of one line (0 or 1), _cols characters. Custom characters (0-7) come out as their
		// code; whoever draws them can use cgram().
		std::string line(const uint32_t _line, const uint32_t _cols = 16) const
		{
			std::lock_guard lock(m_mutex);
			std::string s;
			for(uint32_t c = 0; c < _cols && c < Columns; ++c)
				s += static_cast<char>(m_ddram[_line * 0x40 + c]);
			return s;
		}
		std::array<uint8_t, 64> cgram() const { std::lock_guard lock(m_mutex); return m_cgram; }
		bool displayOn() const { return m_displayOn; }
		uint32_t cursor() const { return m_addr; }
		bool cursorOn() const { return m_cursorOn; }
		uint64_t writes() const { return m_writes; }

	private:
		void latch(const bool _rs, const uint8_t _v)
		{
			std::lock_guard lock(m_mutex);
			++m_writes;
			if(_rs)
			{
				if(m_cgMode)
				{
					m_cgram[m_cgAddr & 63] = _v;
					m_cgAddr = (m_cgAddr + (m_increment ? 1 : 63)) & 63;
				}
				else
				{
					m_ddram[m_addr & 0x7f] = _v;
					move();
				}
				return;
			}
			if(_v & 0x80)			// DDRAM address
			{
				m_addr = _v & 0x7f;
				m_cgMode = false;
			}
			else if(_v & 0x40)		// CGRAM address
			{
				m_cgAddr = _v & 0x3f;
				m_cgMode = true;
			}
			else if(_v & 0x20) {}	// function set
			else if(_v & 0x10)		// cursor/display shift
			{
				if(!(_v & 0x08))
					(_v & 0x04) ? move() : moveBack();
			}
			else if(_v & 0x08)		// display on/off
			{
				m_displayOn = (_v & 0x04) != 0;
				m_cursorOn = (_v & 0x02) != 0;
			}
			else if(_v & 0x04)		// entry mode
				m_increment = (_v & 0x02) != 0;
			else if(_v & 0x02)		// home
			{
				m_addr = 0;
				m_cgMode = false;
			}
			else if(_v & 0x01)		// clear
			{
				m_ddram.fill(' ');
				m_addr = 0;
				m_increment = true;
				m_cgMode = false;
			}
		}

		// Two lines: 0x00-0x27 and 0x40-0x67.
		void move()
		{
			if(!m_increment) { moveBack(); return; }
			m_addr = (m_addr == 0x27) ? 0x40 : (m_addr == 0x67 ? 0x00 : (m_addr + 1) & 0x7f);
		}
		void moveBack()
		{
			m_addr = (m_addr == 0x40) ? 0x27 : (m_addr == 0x00 ? 0x67 : (m_addr - 1) & 0x7f);
		}

		mutable std::mutex m_mutex;
		std::array<uint8_t, 128> m_ddram = [] { std::array<uint8_t, 128> a{}; a.fill(' '); return a; }();
		std::array<uint8_t, 64> m_cgram{};
		uint8_t m_bus = 0, m_control = 0;
		bool m_e = false;
		uint32_t m_addr = 0, m_cgAddr = 0;
		bool m_increment = true, m_cgMode = false, m_displayOn = false, m_cursorOn = false;
		uint64_t m_writes = 0;
	};
}
