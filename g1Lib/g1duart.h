#pragma once

// The G1's PC PORT: an SCN2681/68681-family DUART hanging off the CPU on a parallel bus
// improvised with the 68331's ports (see NOTES.md):
//   - data: the timer's GP port (DDRGP at $FF to write, $00 to read)
//   - control: port E. Bit 0 = /CS, bit 1 = /RD, bit 2 = /WR,
//     bits 3, 6 and 7 = register A0, A1 and A2.
//   - byte received: RxRDY goes to the PAI pin; the OS leaves the pulse accumulator at
//     $FF and the overflow interrupt (PAOV) reads the byte.
// The regular MIDI IN/OUT is the CPU's SCI, not this.
//
// Only channel A and what the OS uses are emulated: commands, status and the data
// registers. The speed (CSR/ACR/timer) is ignored: bytes are delivered at 31250 baud
// from outside.

#include <cstdint>
#include <deque>
#include <vector>

namespace g1
{
	class Duart
	{
	public:
		enum Reg : uint8_t { MR = 0, SR_CSR = 1, CR = 2, RHR_THR = 3, IPCR_ACR = 4, ISR_IMR = 5 };

		// What arrives on PC PORT IN (from the editor to the G1).
		void receive(const std::vector<uint8_t>& _bytes) { m_rx.insert(m_rx.end(), _bytes.begin(), _bytes.end()); }
		bool hasRx() const { return m_rxEnabled && !m_rx.empty(); }

		// What the G1 has sent out on PC PORT OUT.
		void takeTx(std::vector<uint8_t>& _out)
		{
			_out.insert(_out.end(), m_tx.begin(), m_tx.end());
			m_tx.clear();
		}

		uint8_t read(const uint8_t _reg)
		{
			switch(_reg)
			{
			case SR_CSR:
				// RxRDY, TxRDY and TxEMT: the transmitter is always free.
				return static_cast<uint8_t>((hasRx() ? 0x01 : 0) | 0x04 | 0x08);
			case RHR_THR:
				if(m_rx.empty())
					return 0;
				{
					const auto b = m_rx.front();
					m_rx.pop_front();
					++m_rxCount;
					return b;
				}
			case ISR_IMR:
				return static_cast<uint8_t>(0x01 | (hasRx() ? 0x02 : 0));
			default:
				return 0;
			}
		}

		void write(const uint8_t _reg, const uint8_t _val)
		{
			++m_writes[_reg & 7];
			switch(_reg)
			{
			case CR:
				if((_val & 0x03) == 0x01) m_rxEnabled = true;
				if((_val & 0x03) == 0x02) m_rxEnabled = false;
				if((_val & 0xf0) == 0x20) m_rx.clear();	// receiver reset
				break;
			case RHR_THR:
				m_tx.push_back(_val);
				++m_txCount;
				break;
			default:
				break;
			}
		}

		uint32_t rxCount() const { return m_rxCount; }
		uint32_t txCount() const { return m_txCount; }
		uint32_t writes(uint8_t _reg) const { return m_writes[_reg & 7]; }

	private:
		std::deque<uint8_t> m_rx;
		std::vector<uint8_t> m_tx;
		bool m_rxEnabled = false;
		uint32_t m_rxCount = 0;
		uint32_t m_txCount = 0;
		uint32_t m_writes[8]{};
	};
}
