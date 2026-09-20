#pragma once

// Between the emulator and whatever sound system is underneath. It knows nothing about JACK,
// ALSA or JUCE: the backends (jackaudio.h, juceaudio.h, alsaaudio.h) own one of these and hand it
// their buffers.
//
// The G1 runs at 96 kHz and the sound card at whatever it says (usually 48): linear interpolation
// converts both ways. Between the emulator thread and the audio thread there are two lock-free
// queues, one producer and one consumer each. If the emulator falls behind, the audio thread puts
// out silence, the dropout is counted, and a cushion is filled again before playing resumes.

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
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

	class AudioBridge
	{
	public:
		static constexpr double EmuRate = 96000.0;		// the G1's sample rate
		static constexpr size_t Outs = 4, Ins = 2;		// the back panel: four outputs, two inputs

		explicit AudioBridge(const float _gain) : m_gain(_gain), m_out(1 << 15), m_in(1 << 15) {}

		// Called when the device opens, and again if it reopens at another rate.
		void setRate(const uint32_t _rate)
		{
			m_rate = _rate ? _rate : 48000;
			m_prefill = static_cast<size_t>(m_rate * 0.03);		// a 30 ms cushion
			m_buffering = true;
		}

		uint32_t rate() const { return m_rate; }
		uint64_t xruns() const { return m_xruns; }
		float peak() { return m_peak.exchange(0.0f); }
		void setGain(const float _gain) { m_gain.store(_gain, std::memory_order_relaxed); }

		// One sample of the four outputs at 96 kHz, signed 24-bit (emulator thread).
		void push(const int32_t _o1, const int32_t _o2, const int32_t _o3, const int32_t _o4)
		{
			const float scale = m_gain.load(std::memory_order_relaxed) / 8388608.0f;
			const std::array<float, Outs> cur{_o1 * scale, _o2 * scale, _o3 * scale, _o4 * scale};
			const float a = std::max(std::fabs(cur[0]), std::fabs(cur[1]));
			if(a > m_peak.load(std::memory_order_relaxed))
				m_peak.store(a, std::memory_order_relaxed);
			// From 96 kHz to the device's rate: one output sample every EmuRate/rate input samples.
			const double step = EmuRate / m_rate;
			while(m_outPos < 1.0)
			{
				std::array<float, Outs> f;
				for(size_t c = 0; c < Outs; ++c)
					f[c] = std::clamp(static_cast<float>(m_prevOut[c] + (cur[c] - m_prevOut[c]) * m_outPos), -1.0f, 1.0f);
				m_out.push(f);	// full: the emulator is ahead of the card; dropped
				m_outPos += step;
			}
			m_outPos -= 1.0;
			m_prevOut = cur;
		}

		// The L/R inputs at 96 kHz, signed 24-bit (DSP 0 thread).
		void pullInput(int32_t& _l, int32_t& _r)
		{
			const double step = m_rate / EmuRate;	// device samples per emulator sample
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

		// One block on the audio thread. _outs and _ins are arrays of channel pointers; a null
		// channel, or fewer than the back panel has, is fine: a stereo card gets outputs 1 and 2.
		void render(float* const* _outs, const size_t _numOuts, const float* const* _ins, const size_t _numIns, const size_t _frames)
		{
			if(m_buffering && m_out.available() >= m_prefill + _frames)
				m_buffering = false;

			for(size_t k = 0; k < _frames; ++k)
			{
				std::array<float, Outs> f{};
				if(!m_buffering && !m_out.pop(f))
				{
					m_buffering = true;	// the emulator is late: a gap
					++m_xruns;
				}
				for(size_t c = 0; c < _numOuts; ++c)
					if(_outs[c])
						_outs[c][k] = c < Outs ? f[c] : 0.0f;

				std::array<float, Ins> in{};
				for(size_t c = 0; c < Ins && c < _numIns; ++c)
					if(_ins[c])
						in[c] = _ins[c][k];
				m_in.push(in);
			}

			// If the emulator runs ahead (its clock is not the sound card's), the cushion is
			// trimmed so the latency does not grow without bound.
			while(m_out.available() > m_prefill * 4)
			{
				std::array<float, Outs> drop;
				m_out.pop(drop);
			}
		}

	private:
		std::atomic<float> m_gain;
		FrameRing<Outs> m_out;
		FrameRing<Ins> m_in;
		uint32_t m_rate = 48000;
		size_t m_prefill = 1440;
		bool m_buffering = true;
		double m_outPos = 0.0, m_inPos = 0.0;
		std::array<float, Outs> m_prevOut{};
		std::array<float, Ins> m_prevIn{}, m_curIn{};
		std::atomic<uint64_t> m_xruns{0};
		std::atomic<float> m_peak{0.0f};
	};
}
