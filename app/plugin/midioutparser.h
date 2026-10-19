#pragma once

// MidiOutParser: the G1's MIDI OUT, a byte at a time, back into whole messages for the host.
//
// The OS writes the SCI a byte at a time (its queues: G1-Mods' notes on the MIDI out), so a
// plugin's MIDI output has to put them together again: channel messages with or without running
// status, the system common ones, and the real-time bytes on their own, wherever they fall. SysEx
// is not passed on in this version: it is skipped up to its end, and a status byte ends it too.
//
// No allocation: it runs on the runner's worker, between slices of emulation.

#include <cstdint>

namespace g1app
{
	class MidiOutParser
	{
	public:
		struct Message
		{
			uint8_t size = 0;
			uint8_t bytes[3] = {};
		};

		// One byte from the G1. True when it completes a message, which is then in _msg.
		bool feed(const uint8_t _b, Message& _msg)
		{
			if(_b >= 0xf8)	// real time: alone, and it leaves everything else as it was
			{
				_msg.size = 1;
				_msg.bytes[0] = _b;
				return true;
			}
			if(_b & 0x80)
			{
				m_sysex = _b == 0xf0;
				m_have = 0;
				if(_b >= 0xf0)
				{
					m_status = 0;	// system common cancels running status
					if(_b == 0xf6)
					{
						_msg.size = 1;
						_msg.bytes[0] = _b;
						return true;
					}
					if(_b == 0xf1 || _b == 0xf2 || _b == 0xf3)
						m_common = _b;
					else
						m_common = 0;	// SysEx and its end, and the undefined ones: nothing to send
					return false;
				}
				m_status = _b;
				m_common = 0;
				return false;
			}
			if(m_sysex)
				return false;
			const uint8_t status = m_common ? m_common : m_status;
			if(!status)
				return false;	// data with no status: the start of a stream, or after SysEx
			m_data[m_have++] = _b;
			if(m_have < dataBytes(status))
				return false;
			_msg.size = static_cast<uint8_t>(1 + m_have);
			_msg.bytes[0] = status;
			_msg.bytes[1] = m_data[0];
			_msg.bytes[2] = m_have > 1 ? m_data[1] : 0;
			m_have = 0;
			if(m_common)
				m_common = 0;	// no running status for those
			return true;
		}

	private:
		static uint8_t dataBytes(const uint8_t _status)
		{
			switch(_status & 0xf0)
			{
			case 0xc0:
			case 0xd0:
				return 1;
			case 0xf0:
				return _status == 0xf2 ? 2 : 1;
			default:
				return 2;
			}
		}

		uint8_t m_status = 0;	// the running status of channel messages
		uint8_t m_common = 0;	// a system common message under way
		uint8_t m_data[2] = {};
		uint8_t m_have = 0;
		bool m_sysex = false;
	};
}
