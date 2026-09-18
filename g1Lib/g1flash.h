#pragma once

// La flash de 1 MB del G1 en $300000, donde vive el OS instalado. El OS acepta un
// Intel 28F008 ($89/$A6), un Fujitsu MBM29F080 ($04/$D5) o un AMD Am29F080 ($01/$D5):
// aqui se emula el AMD, con su juego de ordenes de 8 bits. Programar y borrar son
// instantaneos, asi que cualquier sondeo de estado ve la operacion ya terminada.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace g1
{
	class Flash
	{
	public:
		static constexpr uint32_t Size = 0x100000;
		static constexpr uint32_t SectorSize = 0x10000;	// Am29F080: 16 sectores de 64 KB
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
				case 2: return 0;	// sector no protegido
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
				m_data[_offset & (Size - 1)] &= _val;	// en flash solo se pueden bajar bits
				++m_programmed;
				m_state = State::Idle;
				return;
			}

			if(_val == 0xf0)	// reset: vuelve a leer el contenido
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
				if(_val == 0x10 && cmdAddr == 0x555)	// borrado completo
				{
					std::fill(m_data.begin(), m_data.end(), 0xff);
					m_erased += Size / SectorSize;
				}
				else if(_val == 0x30)					// borrado de un sector
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
