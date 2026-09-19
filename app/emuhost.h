#pragma once

// EmuHost: el G1 emulado funcionando, con todo lo que lo rodea: la flash en disco, los puertos
// MIDI de ALSA (PC Port y MIDI), el audio (JACK o ALSA) y el ritmo de tiempo real, en un hilo
// propio. Lo usan la consola (g1run) y la ventana (g1gui).
//
// Variables de entorno: G1_AUDIO (jack, alsa, dispositivo ALSA o no), G1_GAIN_DB (+36 por
// defecto), G1_JACK_CONNECT=0, G1_RECORD=segundos (WAV de 4 canales junto a la flash).

#include "g1Lib/g1mc.h"

#include <atomic>
#include <fstream>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace g1app
{
	class AlsaMidi;
	class AlsaAudio;
	class JackAudio;

	class EmuHost
	{
	public:
		struct Stats
		{
			double seconds = 0;			// tiempo desde el arranque
			double speed = 0;			// % del tiempo real
			double load = 0;			// % del tiempo que el hilo del emulador no descansa
			double cpuCores = 0;		// nucleos que usa el proceso (los hilos de los DSP esperan girando)
			bool dspOn[g1::g_dspCount] = {};
			uint64_t pcIn = 0, pcOut = 0, midiIn = 0, midiOut = 0;
			float peak = 0;				// pico de las salidas 1/2 (0-1) desde la ultima consulta
			uint64_t xruns = 0;
			std::string audio;			// que salida de audio hay
			std::string midi;			// los puertos MIDI
		};

		EmuHost();
		~EmuHost();

		// Carga la ROM y la flash (o instala el OS de fabrica) y arranca el hilo. _log recibe los
		// mensajes de arranque.
		bool start(const std::string& _romPath, const std::string& _flashPath, std::string& _log);
		void stop();
		bool running() const { return m_thread.joinable(); }

		// El G1. El panel (getLcd, ledRow, setButton, setAdc) se puede usar desde otro hilo.
		g1::Microcontroller& mc() { return *m_mc; }

		Stats stats();

		// Lo mismo que g1run escribia cada 5 s (velocidad, DSP, HI08, audio por DSP).
		std::string report();

		static std::string defaultFlashPath();

	private:
		void run();
		void saveFlash();
		void finishWav();

		std::unique_ptr<g1::Microcontroller> m_mc;
		std::unique_ptr<AlsaMidi> m_midi;
		std::unique_ptr<AlsaAudio> m_alsa;
		std::unique_ptr<JackAudio> m_jack;
		int m_pcPort = -1, m_midiPort = -1;
		std::string m_flashPath;
		std::thread m_thread;
		std::atomic<bool> m_quit{false};

		std::mutex m_statsMutex;
		Stats m_stats;
		std::atomic<uint64_t> m_pcIn{0}, m_pcOut{0}, m_midiIn{0}, m_midiOut{0};
		std::vector<uint64_t> m_lastFrames = std::vector<uint64_t>(g1::g_dspCount, 0);
		double m_lastReportTime = 0;
		uint64_t m_lastReportCycles = 0;

		// WAV (G1_RECORD)
		std::unique_ptr<std::ofstream> m_wav;
		std::string m_wavPath;
		uint64_t m_wavFrames = 0, m_wavMaxFrames = 0;
	};
}
