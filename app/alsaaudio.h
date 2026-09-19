#pragma once

// Salida de audio por ALSA (dispositivo "default": con PipeWire aparece como un cliente
// mas). El emulador empuja tramas estereo a 96 kHz desde su hilo; un hilo propio las pasa a
// 48 kHz (media de cada par) y las escribe. Si el emulador se queda corto, ALSA se queda sin
// datos: se espera a tener un colchon y se cuenta el corte (PipeWire no avisa de los xrun).

#include <alsa/asoundlib.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace g1app
{
	class AlsaAudio
	{
	public:
		static constexpr uint32_t Rate = 48000;
		static constexpr uint32_t Block = 240;			// 5 ms por escritura
		static constexpr uint32_t Prefill = Block * 6;	// 30 ms de colchon tras un corte

		AlsaAudio(const char* _device, float _gain) : m_gain(_gain)
		{
			if(snd_pcm_open(&m_pcm, _device, SND_PCM_STREAM_PLAYBACK, 0) < 0)
			{
				m_pcm = nullptr;
				return;
			}
			if(snd_pcm_set_params(m_pcm, SND_PCM_FORMAT_FLOAT_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, Rate, 1, 60000) < 0)
			{
				snd_pcm_close(m_pcm);
				m_pcm = nullptr;
				return;
			}
			m_thread = std::thread([this] { run(); });
		}

		~AlsaAudio()
		{
			m_quit = true;
			if(m_thread.joinable())
				m_thread.join();
			if(m_pcm)
			{
				snd_pcm_drop(m_pcm);
				snd_pcm_close(m_pcm);
			}
		}

		bool valid() const { return m_pcm != nullptr; }
		uint64_t xruns() const { return m_xruns; }
		float peak() { return m_peak.exchange(0.0f); }

		// Una trama a 96 kHz, en 24 bits con signo. Se acumulan de dos en dos.
		void push(const int32_t _l, const int32_t _r)
		{
			m_acc[0] += static_cast<float>(_l);
			m_acc[1] += static_cast<float>(_r);
			if(++m_accCount < 2)
				return;
			const float scale = m_gain / (2.0f * 8388608.0f);
			const float l = m_acc[0] * scale, r = m_acc[1] * scale;
			m_acc = {0.0f, 0.0f};
			m_accCount = 0;
			const float a = std::max(std::fabs(l), std::fabs(r));
			if(a > m_peak.load(std::memory_order_relaxed))
				m_peak.store(a, std::memory_order_relaxed);
			std::lock_guard lock(m_mutex);
			if(m_fifo.size() < Rate * 2)	// como mucho 1 s: si el audio no avanza, no crece sin fin
			{
				m_fifo.push_back(std::clamp(l, -1.0f, 1.0f));
				m_fifo.push_back(std::clamp(r, -1.0f, 1.0f));
			}
		}

	private:
		void run()
		{
			std::vector<float> block(Block * 2);
			bool buffering = true;
			while(!m_quit)
			{
				{
					std::lock_guard lock(m_mutex);
					const auto frames = m_fifo.size() / 2;
					if(buffering && frames >= Prefill)
						buffering = false;
					if(!buffering && frames >= Block)
					{
						std::copy_n(m_fifo.begin(), block.size(), block.begin());
						m_fifo.erase(m_fifo.begin(), m_fifo.begin() + static_cast<std::ptrdiff_t>(block.size()));
					}
					else
					{
						if(!buffering)
							++m_xruns;	// el emulador no llega: hueco en el audio
						buffering = true;
						block.clear();
					}
				}
				if(block.empty())
				{
					block.resize(Block * 2);
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				const auto n = snd_pcm_writei(m_pcm, block.data(), Block);
				if(n < 0)
				{
					++m_xruns;
					snd_pcm_recover(m_pcm, static_cast<int>(n), 1);
					buffering = true;
				}
			}
		}

		snd_pcm_t* m_pcm = nullptr;
		const float m_gain;
		std::array<float, 2> m_acc{};
		uint32_t m_accCount = 0;
		std::mutex m_mutex;
		std::vector<float> m_fifo;
		std::thread m_thread;
		std::atomic<bool> m_quit{false};
		std::atomic<uint64_t> m_xruns{0};
		std::atomic<float> m_peak{0.0f};
	};
}
