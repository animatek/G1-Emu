#pragma once

// EmuHost: the emulated G1 running, with everything around it: the flash on disk, the ALSA
// MIDI ports (PC Port and MIDI), the audio (JACK or ALSA) and real-time pacing, on a thread
// of its own. Used by the console (g1run) and the window (g1gui).
//
// Environment variables: G1_AUDIO (jack, alsa, an ALSA device or no), G1_GAIN_DB (+36 by
// default), G1_JACK_CONNECT=0, G1_RECORD=seconds (4-channel WAV next to the flash).

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
			double seconds = 0;			// time since start
			double speed = 0;			// % of real time
			double load = 0;			// % of the time the emulator thread is busy
			double cpuCores = 0;		// cores used by the process (the DSP threads spin while waiting)
			bool dspOn[g1::g_dspCount] = {};
			uint64_t pcIn = 0, pcOut = 0, midiIn = 0, midiOut = 0;
			float peak = 0;				// peak of outputs 1/2 (0-1) since the last query
			uint64_t xruns = 0;
			std::string audio;			// which audio output is in use
			std::string midi;			// the MIDI ports
		};

		EmuHost();
		~EmuHost();

		// Loads the ROM and the flash (or installs the factory OS) and starts the thread. _log gets
		// the startup messages.
		bool start(const std::string& _romPath, const std::string& _flashPath, std::string& _log);
		void stop();
		bool running() const { return m_thread.joinable(); }

		// The G1. The panel (getLcd, ledRow, setButton, setAdc) can be used from another thread.
		g1::Microcontroller& mc() { return *m_mc; }

		Stats stats();

		// What g1run used to print every 5 s (speed, DSPs, HI08, audio per DSP).
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
