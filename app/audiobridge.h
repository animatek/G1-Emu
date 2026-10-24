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
#include <cstring>
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

		// Consumer only: release a batch without copying samples that will not be heard.
		size_t skip(const size_t _frames)
		{
			const auto r = m_read.load(std::memory_order_relaxed);
			const auto n = std::min(_frames, m_write.load(std::memory_order_acquire) - r);
			m_read.store(r + n, std::memory_order_release);
			return n;
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
		// Serialize with render() (the device's stopped/about-to-start lifecycle); DSP push()
		// and pullInput() may continue concurrently.
		void setRate(const uint32_t _rate)
		{
			const auto rate = _rate ? _rate : 48000;
			m_rate.store(rate, std::memory_order_relaxed);
			m_prefill = static_cast<size_t>(rate * 0.03);		// a 30 ms cushion
			m_buffering = true;
		}

		uint32_t rate() const { return m_rate.load(std::memory_order_relaxed); }
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
			const double step = EmuRate / rate();
			while(m_outPos < 1.0)
			{
				std::array<float, Outs> f;
				for(size_t c = 0; c < Outs; ++c)
					f[c] = std::clamp(static_cast<float>(m_prevOut[c] + (cur[c] - m_prevOut[c]) * m_outPos), -1.0f, 1.0f);
				m_out.push(f);	// full: the emulator is ahead of the card; dropped
				m_produced.store(m_produced.load(std::memory_order_relaxed) + 1, std::memory_order_release);
				m_outPos += step;
			}
			m_outPos -= 1.0;
			m_prevOut = cur;
		}

		// The L/R inputs at 96 kHz, signed 24-bit (DSP 0 thread).
		void pullInput(int32_t& _l, int32_t& _r)
		{
			const double step = rate() / EmuRate;	// device samples per emulator sample
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
						in[c] = cleanInput(_ins[c][k]);
				m_in.push(in);
			}

			// If the emulator runs ahead (its clock is not the sound card's), the cushion is
			// trimmed so the latency does not grow without bound.
			const auto queued = m_out.available();
			if(queued > m_prefill * 4)
				m_out.skip(queued - m_prefill * 4);
		}

		// For a consumer that paces the emulator itself instead of buffering against a card that
		// runs on its own clock (the plugin, plugin/runner.h). No cushion and no trimming: it is the
		// caller who keeps the emulator exactly as far ahead as it wants.
		//
		// Frames produced at the device rate since this bridge was made (emulator thread).
		uint64_t produced() const { return m_produced.load(std::memory_order_acquire); }
		size_t queued() const { return m_out.available(); }

		// Up to _frames frames into _outs from _offset on; returns how many there were. The rest is
		// left untouched (audio thread).
		size_t pull(float* const* _outs, const size_t _numOuts, const size_t _offset, const size_t _frames)
		{
			size_t k = 0;
			for(std::array<float, Outs> f; k < _frames && m_out.pop(f); ++k)
				for(size_t c = 0; c < _numOuts && c < Outs; ++c)
					if(_outs[c])
						_outs[c][_offset + k] = f[c];
			return k;
		}

		// Throws away up to _frames frames; returns how many (audio thread).
		size_t skip(const size_t _frames)
		{
			return m_out.skip(_frames);
		}

		// The L/R inputs, for pullInput() to hand to DSP 0 (audio thread).
		void pushInputs(const float* const* _ins, const size_t _numIns, const size_t _offset, const size_t _frames)
		{
			for(size_t k = 0; k < _frames; ++k)
			{
				std::array<float, Ins> in{};
				for(size_t c = 0; c < Ins && c < _numIns; ++c)
					if(_ins[c])
						in[c] = cleanInput(_ins[c][_offset + k]);
				m_in.push(in);
			}
		}

	private:
		// Sanitize before interpolation: NaN survives clamp, and even finite extremes can
		// overflow the subtraction. Neither may reach the float-to-integer DSP input.
		static float cleanInput(const float _v)
		{
			// Integer logic only: Release builds with -Ofast, where the compiler may assume there is
			// no NaN or infinity, and on arm64 (macOS) a clamp of a NaN that was selected away came
			// out as -1 (v0.1.0-alpha.25's CI). The float is only ever returned as it came or as +-1.
			uint32_t bits;
			static_assert(sizeof(bits) == sizeof(_v));
			std::memcpy(&bits, &_v, sizeof(bits));
			const uint32_t magnitude = bits & 0x7fffffffu;
			if(magnitude >= 0x7f800000u)		// infinity or NaN
				return 0.0f;
			if(magnitude >= 0x3f800000u)		// 1.0 and beyond, up to the largest finite value
				return (bits & 0x80000000u) ? -1.0f : 1.0f;
			return _v;
		}

		std::atomic<uint64_t> m_produced{0};
		std::atomic<float> m_gain;
		FrameRing<Outs> m_out;
		FrameRing<Ins> m_in;
		// Device restarts can change the rate while the emulator/DSP threads are running.
		std::atomic<uint32_t> m_rate{48000};
		size_t m_prefill = 1440;
		bool m_buffering = true;
		double m_outPos = 0.0, m_inPos = 0.0;
		std::array<float, Outs> m_prevOut{};
		std::array<float, Ins> m_prevIn{}, m_curIn{};
		std::atomic<uint64_t> m_xruns{0};
		std::atomic<float> m_peak{0.0f};
	};
}
