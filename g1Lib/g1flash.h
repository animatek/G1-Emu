#pragma once

// The G1's 1 MB flash at $300000, where the installed OS lives. The OS accepts an
// Intel 28F008 ($89/$A6), a Fujitsu MBM29F080 ($04/$D5) or an AMD Am29F080 ($01/$D5):
// the AMD is emulated here, with its 8-bit command set. Programming and erasing are
// instantaneous, so any status poll sees the operation already finished.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace g1
{
	class Flash
	{
	public:
		static constexpr uint32_t Size = 0x100000;
		static constexpr uint32_t SectorSize = 0x10000;	// Am29F080: 16 sectors of 64 KB
		static constexpr uint8_t ManufacturerId = 0x01;	// AMD
		static constexpr uint8_t DeviceId = 0xd5;			// Am29F080

		Flash() : m_data(Size, 0xff) {}

		std::vector<uint8_t>& data() { return m_data; }
		uint32_t programmedBytes() const { return m_programmed; }
		uint32_t erasedSectors() const { return m_erased; }

		uint8_t read(const uint32_t _offset) const
		{
			if(m_autoselect)
			{
				switch(_offset & 0xff)
				{
				case 0: return ManufacturerId;
				case 1: return DeviceId;
				case 2: return 0;	// sector not protected
				default: return 0;
				}
			}
			return m_data[_offset & (Size - 1)];
		}

		void write(const uint32_t _offset, const uint8_t _val)
		{
			const auto cmdAddr = _offset & 0x7ff;

			if(m_state == State::Program)
			{
				m_data[_offset & (Size - 1)] &= _val;	// flash can only clear bits
				++m_programmed;
				m_state = State::Idle;
				return;
			}

			if(_val == 0xf0)	// reset: back to reading the contents
			{
				m_state = State::Idle;
				m_autoselect = false;
				return;
			}

			switch(m_state)
			{
			case State::Idle:
				m_state = (cmdAddr == 0x555 && _val == 0xaa) ? State::Unlock1 : State::Idle;
				break;
			case State::Unlock1:
				m_state = (cmdAddr == 0x2aa && _val == 0x55) ? State::Unlock2 : State::Idle;
				break;
			case State::Unlock2:
				m_state = State::Idle;
				if(cmdAddr != 0x555)
					break;
				if(_val == 0x90) m_autoselect = true;
				else if(_val == 0xa0) m_state = State::Program;
				else if(_val == 0x80) m_state = State::EraseSetup;
				break;
			case State::EraseSetup:
				m_state = (cmdAddr == 0x555 && _val == 0xaa) ? State::EraseUnlock1 : State::Idle;
				break;
			case State::EraseUnlock1:
				m_state = (cmdAddr == 0x2aa && _val == 0x55) ? State::EraseUnlock2 : State::Idle;
				break;
			case State::EraseUnlock2:
				m_state = State::Idle;
				if(_val == 0x10 && cmdAddr == 0x555)	// chip erase
				{
					std::fill(m_data.begin(), m_data.end(), 0xff);
					m_erased += Size / SectorSize;
				}
				else if(_val == 0x30)					// sector erase
				{
					const auto base = (_offset & (Size - 1)) & ~(SectorSize - 1);
					std::fill_n(m_data.begin() + base, SectorSize, 0xff);
					++m_erased;
				}
				break;
			case State::Program:
				break;
			}
		}

	private:
		enum class State { Idle, Unlock1, Unlock2, Program, EraseSetup, EraseUnlock1, EraseUnlock2 };

		std::vector<uint8_t> m_data;
		State m_state = State::Idle;
		bool m_autoselect = false;
		uint32_t m_programmed = 0;
		uint32_t m_erased = 0;
	};
}
