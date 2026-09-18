#pragma once

// El PC PORT del G1: un DUART de la familia SCN2681/68681 colgado de la CPU por un bus
// paralelo improvisado con los puertos del 68331 (ver NOTAS.md):
//   - datos: puerto GP del temporizador (DDRGP a $FF para escribir, a $00 para leer)
//   - control: puerto E. Bit 0 = /CS, bit 1 = /RD, bit 2 = /WR,
//     bits 3, 6 y 7 = A0, A1 y A2 del registro.
//   - aviso de byte recibido: RxRDY va a la patilla PAI; el OS deja el acumulador de
//     pulsos en $FF y la interrupcion de desbordamiento (PAOV) lee el byte.
// El MIDI IN/OUT normal es la SCI de la CPU, no esto.
//
// Solo se emula el canal A y lo que el OS usa: ordenes, estado, y los registros de
// datos. La velocidad (CSR/ACR/temporizador) se ignora: los bytes se entregan al ritmo
// de 31250 baudios desde fuera.

#include <cstdint>
#include <deque>
#include <vector>

namespace g1
{
	class Duart
	{
	public:
		enum Reg : uint8_t { MR = 0, SR_CSR = 1, CR = 2, RHR_THR = 3, IPCR_ACR = 4, ISR_IMR = 5 };

		// Lo que llega por el PC PORT IN (del editor al G1).
		void receive(const std::vector<uint8_t>& _bytes) { m_rx.insert(m_rx.end(), _bytes.begin(), _bytes.end()); }
		bool hasRx() const { return m_rxEnabled && !m_rx.empty(); }

		// Lo que el G1 ha sacado por el PC PORT OUT.
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
				// RxRDY, TxRDY y TxEMT: el transmisor siempre esta libre.
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
				if((_val & 0xf0) == 0x20) m_rx.clear();	// reset del receptor
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
