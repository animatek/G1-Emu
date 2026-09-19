#pragma once

// Audio through JACK (with PipeWire, pipewire-jack): the emulated G1 shows up as the client "G1-Emu"
// with the connectors of the hardware's back panel: out_1..out_4 (mono outputs) and in_L/in_R.
// The G1's headphones are a copy of 1/2, so out_1/out_2 connect themselves to the first two
// physical outputs (G1_JACK_CONNECT=0 prevents it); everything else is routed by hand.
//
// The emulator runs at 96 kHz and JACK at whatever the server says (usually 48 kHz): linear
// interpolation converts both ways. Between the emulator thread and the JACK thread there are
// two lock-free queues (one producer and one consumer each); if the emulator falls behind, JACK
// outputs silence, the dropout is counted and a 30 ms cushion is filled again.

#include <jack/jack.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace g1app
{
	// Lock-free queue of N-channel frames, one producer and one consumer.
	template<size_t N> class FrameRing
	{
	public:
		explicit FrameRing(const size_t _frames) : m_size(roundUp(_frames)), m_data(m_size * N) {}

		size_t available() const { return m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_relaxed); }
		size_t space() const { return m_size - (m_write.load(std::memory_order_relaxed) - m_read.load(std::memory_order_acquire)); }

		bool push(const std::array<float, N>& _f)
		{
			if(space() == 0)
				return false;
			const auto w = m_write.load(std::memory_order_relaxed);
			std::copy(_f.begin(), _f.end(), m_data.begin() + static_cast<std::ptrdiff_t>((w & (m_size - 1)) * N));
			m_write.store(w + 1, std::memory_order_release);
			return true;
		}

		bool pop(std::array<float, N>& _f)
		{
			if(available() == 0)
				return false;
			const auto r = m_read.load(std::memory_order_relaxed);
			std::copy_n(m_data.begin() + static_cast<std::ptrdiff_t>((r & (m_size - 1)) * N), N, _f.begin());
			m_read.store(r + 1, std::memory_order_release);
			return true;
		}

	private:
		static size_t roundUp(size_t _v) { size_t s = 1; while(s < _v) s <<= 1; return s; }
		const size_t m_size;
		std::vector<float> m_data;
		std::atomic<size_t> m_write{0}, m_read{0};
	};

	class JackAudio
	{
	public:
		static constexpr double EmuRate = 96000.0;

		JackAudio(const char* _name, const float _gain) : m_gain(_gain), m_out(1 << 15), m_in(1 << 15)
		{
			m_client = jack_client_open(_name, JackNoStartServer, nullptr);
			if(!m_client)
				return;
			m_rate = jack_get_sample_rate(m_client);
			for(int i = 0; i < 4; ++i)
				m_outPorts[i] = jack_port_register(m_client, ("out_" + std::to_string(i + 1)).c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
			m_inPorts[0] = jack_port_register(m_client, "in_L", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
			m_inPorts[1] = jack_port_register(m_client, "in_R", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
			m_prefill = static_cast<size_t>(m_rate * 0.03);
			jack_set_process_callback(m_client, [](jack_nframes_t _n, void* _arg) { return static_cast<JackAudio*>(_arg)->process(_n); }, this);
			if(jack_activate(m_client))
			{
				jack_client_close(m_client);
				m_client = nullptr;
				return;
			}
			const char* connect = std::getenv("G1_JACK_CONNECT");
			if(!connect || std::string(connect) != "0")
				if(const char** ports = jack_get_ports(m_client, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical | JackPortIsInput))
				{
					for(int i = 0; i < 2 && ports[i]; ++i)
						jack_connect(m_client, jack_port_name(m_outPorts[i]), ports[i]);
					jack_free(ports);
				}
		}

		~JackAudio()
		{
			if(m_client)
			{
				jack_deactivate(m_client);
				jack_client_close(m_client);
			}
		}

		bool valid() const { return m_client != nullptr; }
		uint32_t rate() const { return m_rate; }
		uint64_t xruns() const { return m_xruns; }
		float peak() { return m_peak.exchange(0.0f); }

		// One sample of the four outputs at 96 kHz, signed 24-bit (emulator thread).
		void push(const int32_t _o1, const int32_t _o2, const int32_t _o3, const int32_t _o4)
		{
			const float scale = m_gain / 8388608.0f;
			const std::array<float, 4> cur{_o1 * scale, _o2 * scale, _o3 * scale, _o4 * scale};
			const float a = std::max(std::fabs(cur[0]), std::fabs(cur[1]));
			if(a > m_peak.load(std::memory_order_relaxed))
				m_peak.store(a, std::memory_order_relaxed);
			// From 96 kHz to the JACK rate: one output sample every EmuRate/rate input samples.
			const double step = EmuRate / m_rate;
			while(m_outPos < 1.0)
			{
				std::array<float, 4> f;
				for(size_t c = 0; c < 4; ++c)
					f[c] = std::clamp(static_cast<float>(m_prevOut[c] + (cur[c] - m_prevOut[c]) * m_outPos), -1.0f, 1.0f);
				m_out.push(f);	// full: the emulator is ahead of JACK; dropped
				m_outPos += step;
			}
			m_outPos -= 1.0;
			m_prevOut = cur;
		}

		// The L/R inputs at 96 kHz, signed 24-bit (DSP 0 thread).
		void pullInput(int32_t& _l, int32_t& _r)
		{
			const double step = m_rate / EmuRate;	// JACK samples per emulator sample
			m_inPos += step;
			while(m_inPos >= 1.0)
			{
				m_prevIn = m_curIn;
				if(!m_in.pop(m_curIn))
					m_curIn = m_prevIn;	// no data: hold
				m_inPos -= 1.0;
			}
			const auto t = static_cast<float>(m_inPos);
			_l = static_cast<int32_t>(std::clamp(m_prevIn[0] + (m_curIn[0] - m_prevIn[0]) * t, -1.0f, 1.0f) * 8388607.0f);
			_r = static_cast<int32_t>(std::clamp(m_prevIn[1] + (m_curIn[1] - m_prevIn[1]) * t, -1.0f, 1.0f) * 8388607.0f);
		}

	private:
		int process(const jack_nframes_t _n)
		{
			std::array<float*, 4> out;
			for(int i = 0; i < 4; ++i)
				out[i] = static_cast<float*>(jack_port_get_buffer(m_outPorts[i], _n));
			const auto* inL = static_cast<const float*>(jack_port_get_buffer(m_inPorts[0], _n));
			const auto* inR = static_cast<const float*>(jack_port_get_buffer(m_inPorts[1], _n));

			if(m_buffering && m_out.available() >= m_prefill + _n)
				m_buffering = false;
			for(jack_nframes_t k = 0; k < _n; ++k)
			{
				std::array<float, 4> f{};
				if(!m_buffering && !m_out.pop(f))
				{
					m_buffering = true;	// the emulator is late: a gap
					++m_xruns;
				}
				for(int c = 0; c < 4; ++c)
					out[c][k] = f[c];
				m_in.push({inL[k], inR[k]});
			}
			// If the emulator runs ahead (a clock different from the sound card's), the cushion is
			// trimmed so the latency does not grow.
			while(m_out.available() > m_prefill * 4)
			{
				std::array<float, 4> drop;
				m_out.pop(drop);
			}
			return 0;
		}

		jack_client_t* m_client = nullptr;
		uint32_t m_rate = 48000;
		std::array<jack_port_t*, 4> m_outPorts{};
		std::array<jack_port_t*, 2> m_inPorts{};
		const float m_gain;
		FrameRing<4> m_out;
		FrameRing<2> m_in;
		size_t m_prefill = 1440;
		bool m_buffering = true;
		double m_outPos = 0.0, m_inPos = 0.0;
		std::array<float, 4> m_prevOut{};
		std::array<float, 2> m_prevIn{}, m_curIn{};
		std::atomic<uint64_t> m_xruns{0};
		std::atomic<float> m_peak{0.0f};
	};
}
