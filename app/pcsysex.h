#pragma once

// The PC Port's SysEx, as the SlotKeeper and the SynthSettingsLink speak it to the OS: Clavia's
// frame (F0 33, cc and slot, 06, payload, checksum, F7), the 7-bit packing of patch packets, and
// whole messages out of a byte stream.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace g1app::pcsysex
{
	inline std::vector<uint8_t> frame(const uint8_t _cc, const uint8_t _slot, const std::vector<uint8_t>& _payload)
	{
		std::vector<uint8_t> m = {0xf0, 0x33, static_cast<uint8_t>(((_cc & 0x1f) << 2) | (_slot & 3)), 0x06};
		m.insert(m.end(), _payload.begin(), _payload.end());
		uint32_t sum = 0;
		for(const auto b : m)
			sum += b;
		m.push_back(static_cast<uint8_t>(sum & 0x7f));
		m.push_back(0xf7);
		return m;
	}

	inline std::vector<uint8_t> pack7(const std::vector<uint8_t>& _raw)
	{
		std::vector<uint8_t> out;
		uint32_t buffer = 0;
		int held = 0;
		for(const auto b : _raw)
		{
			buffer = (buffer << 8) | b;
			held += 8;
			while(held >= 7)
			{
				held -= 7;
				out.push_back(static_cast<uint8_t>((buffer >> held) & 0x7f));
			}
		}
		if(held > 0)
			out.push_back(static_cast<uint8_t>((buffer << (7 - held)) & 0x7f));
		return out;
	}

	inline std::vector<uint8_t> unpack7(const std::vector<uint8_t>& _in)
	{
		std::vector<uint8_t> out;
		uint32_t buffer = 0;
		int held = 0;
		for(const auto c : _in)
		{
			buffer = (buffer << 7) | (c & 0x7f);
			held += 7;
			if(held >= 8)
			{
				held -= 8;
				out.push_back(static_cast<uint8_t>((buffer >> held) & 0xff));
			}
		}
		return out;
	}

	// Whole SysEx messages out of a byte stream; what is left over stays in _buf.
	template<typename F> void forEachMessage(std::vector<uint8_t>& _buf, F&& _f)
	{
		size_t pos = 0;
		for(;;)
		{
			const auto start = std::find(_buf.begin() + static_cast<long>(pos), _buf.end(), uint8_t(0xf0));
			if(start == _buf.end())
			{
				pos = _buf.size();
				break;
			}
			const auto end = std::find(start, _buf.end(), uint8_t(0xf7));
			if(end == _buf.end())
			{
				pos = static_cast<size_t>(start - _buf.begin());
				break;
			}
			_f(std::vector<uint8_t>(start, end + 1));
			pos = static_cast<size_t>(end - _buf.begin()) + 1;
		}
		_buf.erase(_buf.begin(), _buf.begin() + static_cast<long>(pos));
	}

	// Whether a byte stream has stopped inside a SysEx: an F0 with no F7 yet (issue #81). The editor's
	// bytes go to the PC Port as they come, and a MIDI driver hands a long SysEx over in pieces, so the
	// first piece of an upload's packet can be in the G1 while the rest is on its way. Whoever else talks
	// to the PC Port (the slot keeper, the settings and presets links) must wait for the rest: a request
	// in the middle of the editor's message breaks it, and the OS rejects the upload (code 3 or 6). Any
	// status byte but a real-time one ends a SysEx, as MIDI has it.
	struct SysexTracker
	{
		bool inside = false;
		void feed(const std::vector<uint8_t>& _bytes)
		{
			for(const auto b : _bytes)
			{
				if(b == 0xf0)
					inside = true;
				else if(b >= 0x80 && b < 0xf8)
					inside = false;
			}
		}
	};

	inline bool isClavia(const std::vector<uint8_t>& _m) { return _m.size() >= 6 && _m[1] == 0x33; }
	inline uint8_t ccOf(const std::vector<uint8_t>& _m) { return static_cast<uint8_t>(_m[2] >> 2); }
	inline uint8_t slotOf(const std::vector<uint8_t>& _m) { return static_cast<uint8_t>(_m[2] & 3); }
	inline bool isPacket(const uint8_t _cc) { return _cc >= 0x1c && _cc <= 0x1f; }	// a patch packet, first/last in bits 0/1
}
