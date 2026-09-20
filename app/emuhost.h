#pragma once

// EmuHost: the emulated G1 running, with everything around it: the flash on disk, the ALSA
// MIDI ports (PC Port and MIDI), the audio (JACK or ALSA) and real-time pacing, on a thread
// of its own. Used by the console (g1run) and the window (g1gui).
//
// What it uses is in Options: the window sets them from its settings panel and g1run leaves them
// at their defaults. The G1_* environment variables still win over both, so scripts and the test
// bench keep working: G1_AUDIO (jack, alsa, an ALSA device or no), G1_GAIN_DB (+36 by default),
// G1_JACK_CONNECT=0, G1_RAWMIDI (the ID of the snd-virmidi card to take over, G1Emu by default;
// 0 disables it), G1_RECORD=seconds (4-channel WAV next to the flash). G1_THREADS and G1_INTERP
// are debugging knobs of the emulator core and stay environment-only.

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
#ifdef G1_BACKEND_JUCE
	class JuceMidi;
	class JuceAudio;
	using Midi = JuceMidi;
#else
	class AlsaMidi;
	class AlsaAudio;
	class JackAudio;
	using Midi = AlsaMidi;
#endif

	class EmuHost
	{
	public:
		// What the user gets to choose. Set them before start(); the environment still wins.
		struct Options
		{
			std::string audio = "jack";			// "jack", "alsa", "no", or the name of an ALSA device
			float gainDb = 36.0f;				// undoes the -36 dB cap the OS puts on the master volume
			bool jackConnect = true;			// out_1/out_2 connect themselves to the sound card
			std::string rawMidiCard = "G1";		// the raw MIDI card to take over (empty: none)
			std::string rom;					// the ROM to use; empty: look for one (romfinder.h)
			bool showDisclaimer = false;		// the notice at startup; the window can turn it back on

			// The names used in the settings file and in the panel.
			static const char* const audioNames[3];	// "jack", "alsa", "no"

			// A plain "key = value" file next to the flash, so the window and the console agree
			// on what they use. Missing or unreadable, the defaults stand; load() says whether it
			// was there, which is how the window knows it is a first run.
			bool load(const std::string& _path);
			bool save(const std::string& _path) const;
		};

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
			std::string rawMidi;		// the raw MIDI devices linked to them (empty: none)
		};

		EmuHost();
		~EmuHost();

		// Loads the ROM and the flash (or installs the factory OS) and starts the thread. _log gets
		// the startup messages.
		// An empty _romPath means "look for one": the settings and then the ROM folders
		// (romfinder.h). _log gets the startup messages, and when there is no ROM it gets the
		// whole story — where it looked and what was wrong with what it found.
		bool start(const std::string& _romPath, const std::string& _flashPath, std::string& _log);

		// After a start() that failed for want of a ROM: what to tell the user, and where they
		// have to put it. Empty when the failure was something else.
		const std::string& romProblem() const { return m_romProblem; }

		// Before start(): what to use. After it: what was asked for, environment included.
		Options& options() { return m_options; }
		const Options& options() const { return m_options; }

		// The output level, the only setting that can change while it plays.
		void setGainDb(float _gainDb);
		void stop();
		bool running() const { return m_thread.joinable(); }

		// The G1. The panel (getLcd, ledRow, setButton, setAdc) can be used from another thread.
		g1::Microcontroller& mc() { return *m_mc; }

		Stats stats();

		// What g1run used to print every 5 s (speed, DSPs, HI08, audio per DSP).
		std::string report();

		static std::string defaultFlashPath();
		static std::string defaultSettingsPath();

	private:
		void run();
		bool bindRawMidi(std::string& _log);
		void saveFlash();
		void finishWav();

		std::unique_ptr<g1::Microcontroller> m_mc;
		std::unique_ptr<Midi> m_midi;
#ifdef G1_BACKEND_JUCE
		std::unique_ptr<JuceAudio> m_juceAudio;
#else
		std::unique_ptr<AlsaAudio> m_alsa;
		std::unique_ptr<JackAudio> m_jack;
#endif
		int m_pcPort = -1, m_midiPort = -1;
		bool m_rawMidiBound = false;
		std::string m_romProblem;
		Options m_options;
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
