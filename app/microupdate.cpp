#include "microupdate.h"

#include <algorithm>

namespace g1app
{
	namespace
	{
		// The display's letters, active low: a, b, c, d, e, f, g are bits 0-6
		constexpr uint8_t g_blank = 0xff, g_U = 0xc1, g_d = 0xa1, g_S = 0x92, g_t = 0x87, g_E = 0x86, g_r = 0xaf, g_dash = 0xbf;

		// 8 bytes into 7: the first carries the low bit of each of the next seven (LSB first), each of which is (b << 1) | bit
		std::vector<uint8_t> unpack(const uint8_t* _p, const size_t _count)
		{
			std::vector<uint8_t> out;
			size_t j = 0;
			while(out.size() < _count)
			{
				uint8_t lo = _p[j++];
				for(int i = 0; i < 7 && out.size() < _count; ++i, lo >>= 1)
				{
					out.push_back(static_cast<uint8_t>((_p[j] << 1) | (lo & 1)));
					++j;
				}
			}
			return out;
		}

		// 5 packed bytes are a 32-bit number
		uint32_t number(const uint8_t* _p)
		{
			const auto b = unpack(_p, 4);
			return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
		}

		uint8_t swapBits(const uint8_t _x) { return static_cast<uint8_t>(((_x & 0xaa) >> 1) | ((_x & 0x55) << 1)); }
	}

	void MicroUpdate::feed(const std::vector<uint8_t>& _bytes, std::vector<uint8_t>& _reply)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		for(const uint8_t b : _bytes)
		{
			if(m_pending.empty() && b != 0xf0)
				continue;		// between messages: nothing but the start of one
			m_pending.push_back(b);
			if(b == 0xf7)
			{
				message(m_pending, _reply);
				m_pending.clear();
			}
		}
	}

	void MicroUpdate::message(const std::vector<uint8_t>& _m, std::vector<uint8_t>& _reply)
	{
		// F0 33 7F 06 <cmd> body F7
		if(_m.size() < 6 || _m[1] != 0x33 || _m[3] != 0x06)
			return;
		const uint8_t cmd = _m[4];
		const uint8_t* body = _m.data() + 5;
		const size_t n = _m.size() - 6;
		if(m_state == State::Stored)
			return;
		switch(cmd)
		{
		case 0x00:		// the greeting: the IAm of a synth in its update utility (version, 7F, the boot ROM's, the Micro's device byte)
			_reply.insert(_reply.end(), {0xf0, 0x33, 0x00, 0x06, 0x01, 0x03, 0x03, 0x7f, 0x01, 0x08, 0x02, 0xf7});
			break;
		case 0x70:		// the header: address and length (5 packed bytes each), the version, the sum of those 12 bytes & 7F
		{
			if(n != 13)
			{
				m_state = State::Failed;
				m_why = "a header of " + std::to_string(n) + " bytes";
				return;
			}
			unsigned sum = 0;
			for(size_t i = 0; i < 12; ++i)
				sum += body[i];
			if((sum & 0x7f) != body[12])
			{
				m_state = State::Failed;
				m_why = "the header's sum is wrong";
				return;
			}
			m_address = number(body);
			m_length = number(body + 5);
			m_data.clear();
			m_sum = 0;
			m_state = State::Receiving;
			break;
		}
		case 0x71:		// 112 bytes of data, in 128
			if(m_state != State::Receiving || n != 128)
				return;
			for(const uint8_t b : unpack(body, BlockBytes))
				if(m_data.size() < m_length)
				{
					m_data.push_back(b);
					m_sum += b;
				}
			break;
		case 0x73:		// the sum of the data
			if(m_state != State::Receiving || n != 5)
				return;
			if(m_data.size() != m_length || number(body) != m_sum)
			{
				m_state = State::Failed;
				m_why = "the data came to " + std::to_string(m_data.size()) + " of " + std::to_string(m_length) + " bytes, or its sum is wrong";
				return;
			}
			m_state = State::Ready;
			_reply.insert(_reply.end(), {0xf0, 0x33, 0x00, 0x06, 0x7f, 0xf7});
			break;
		default:
			break;
		}
	}

	void MicroUpdate::press()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(m_state != State::Ready)
			return;
		std::vector<uint8_t> image(m_data.size());
		uint8_t key = 17;
		for(size_t i = 0; i < m_data.size(); ++i, key = static_cast<uint8_t>(key + 17))
			image[i] = swapBits(static_cast<uint8_t>(m_data[i] ^ key));
		// An OS starts by masking the interrupts: move.w #$2700,sr
		if(image.size() < 0x10000 || (image.size() & 3) || image.size() > 0x6ffe0 || image[0] != 0x46 || image[1] != 0xfc || image[2] != 0x27 || image[3] != 0x00)
		{
			m_state = State::Failed;
			m_why = "what came in is not an OS image";
			return;
		}
		m_image = std::move(image);
		m_state = State::Stored;
	}

	void MicroUpdate::cancel()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(m_state == State::Ready || m_state == State::Receiving)
		{
			m_state = State::Waiting;
			m_data.clear();
		}
	}

	MicroUpdate::State MicroUpdate::state() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_state;
	}

	std::string MicroUpdate::describe() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		switch(m_state)
		{
		case State::Waiting:   return "update mode: waiting for an OS (display Ud)";
		case State::Receiving: return "update mode: receiving the OS, " + std::to_string(m_data.size()) + " of " + std::to_string(m_length) + " bytes";
		case State::Ready:     return "update mode: the OS is in and its sum is right; a patch button stores it (display St), Shift cancels";
		case State::Stored:    return "update mode: the OS is stored: restart the Micro to run it";
		case State::Failed:    return "update mode: failed (" + m_why + ")";
		}
		return {};
	}

	std::array<uint8_t, 2> MicroUpdate::digits(const uint64_t _nowMs) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		switch(m_state)
		{
		case State::Waiting:
		case State::Receiving: return {{g_d, g_U}};
		case State::Ready:     return (_nowMs / 500) % 2 ? std::array<uint8_t, 2>{{g_blank, g_blank}} : std::array<uint8_t, 2>{{g_t, g_S}};
		case State::Stored:    return {{g_dash, g_dash}};
		case State::Failed:    return {{g_r, g_E}};
		}
		return {{g_blank, g_blank}};
	}

	bool MicroUpdate::storedImage(std::vector<uint8_t>& _os) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(m_state != State::Stored)
			return false;
		_os = m_image;
		return true;
	}
}
