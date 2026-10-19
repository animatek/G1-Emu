#pragma once
#include "diaglog.h"
#include "directlink.h"

// EmuHost: the emulated G1 running, with everything around it: the flash on disk, the ALSA
// MIDI ports (PC Port and MIDI), the audio (JACK or ALSA) and real-time pacing, on a thread
// of its own. Used by the console (g1run) and the window (g1gui).
//
// What it uses is in Options: the window sets them from its settings panel and g1run leaves them
// at their defaults. The G1_* environment variables still win over both, so scripts and the test
// bench keep working: G1_AUDIO (jack, alsa, an ALSA device or no), G1_GAIN_DB (+36 by default),
// G1_JACK_CONNECT=0, G1_RAWMIDI (the ID of the snd-virmidi card to take over, G1Emu by default;
// 0 disables it), G1_RECORD=seconds (4-channel WAV next to the flash), G1_PCPORT_OUT/G1_PCPORT_IN/
// G1_MIDI_OUT/G1_MIDI_IN (manual MIDI device names, see Options::pcPortOutDevice). G1_THREADS and
// G1_INTERP are debugging knobs of the emulator core and stay environment-only.

#include "engine.h"
#include "hostconfig.h"
#include "midirouting.h"
#include "presetslink.h"
#include "synthsettings.h"

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
	class MidiTransport;
#ifdef G1_BACKEND_JUCE
	class JuceAudio;
#else
	class AlsaAudio;
	class JackAudio;
#endif

	class EmuHost
	{
	public:
		// What the user gets to choose (set them before start(); the environment still wins), and
		// what it reports. Both live in hostconfig.h so the plugin can share them without EmuHost.
		using Options = HostOptions;
		using Stats = HostStats;

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

		// Switches the G1 off and on, as its power switch would: the flash is saved and a new G1
		// boots from it, with the knobs where they were. MIDI ports and audio stay open. Whatever
		// holds on to mc() must let it go first: the G1 it returned is gone.
		bool restart(std::string& _log);

		// The G1. The panel (getLcd, ledRow, setButton, setAdc) can be used from another thread.
		g1::Microcontroller& mc() { return m_engine->mc(); }
		// The OS's synth settings (MIDI channels, clock...), through the PC Port between the editor's messages.
		SynthSettingsLink& synthSettings() { return m_synthSettings; }
		PresetsLink& presets() { return m_presets; }
		// The last MIDI controller that came in, for the panel's MIDI Learn (MidiRouting).
		bool lastController(int& _channel, int& _cc, uint32_t& _serial) const { return m_midiRouting.lastController(_channel, _cc, _serial); }
		// MIDI CC Mode on Selected Slots: a controller on a selected slot's channel goes to every selected
		// slot (MidiRouting). Kept in the options, for the window to save.
		bool selectedControllers() const { return m_selectedControllers; }
		void setSelectedControllers(const bool _on) { m_options.selectedControllers = _on; m_selectedControllers = _on; }
		// Octave Shift: On moves each note by its slot's shift, as the keyboard model's keys do; Off
		// plays them as the rack does (MidiRouting). Kept in the options, for the window to save.
		bool octaveShift() const { return m_octaveShift; }
		void setOctaveShift(const bool _on) { m_options.octaveShift = _on; m_octaveShift = _on; }

		Stats stats();

		// What g1run used to print every 5 s (speed, DSPs, HI08, audio per DSP).
		std::string report();

		static std::string defaultFlashPath() { return g1app::defaultFlashPath(); }
		static std::string defaultSettingsPath() { return g1app::defaultSettingsPath(); }

	private:
		void run();
		void boot(bool _update, std::string& _log);
		void launch();
		void wireEngine();
		bool bindRawMidi(std::string& _log);
		void saveFlash();
		void finishWav();

		std::unique_ptr<Engine> m_engine;
		std::unique_ptr<MidiTransport> m_midi;
#ifdef G1_BACKEND_JUCE
		std::unique_ptr<JuceAudio> m_juceAudio;
#else
		std::unique_ptr<AlsaAudio> m_alsa;
		std::unique_ptr<JackAudio> m_jack;
#endif
		int m_pcPort = -1, m_midiPort = -1;
		DirectLink m_link;	// the PC Port for Animatek NME over a local socket, beside the MIDI one
		SynthSettingsLink m_synthSettings;
		PresetsLink m_presets;
		MidiRouting m_midiRouting;	// the notes from the MIDI port, by each slot's octave shift
		std::atomic<bool> m_selectedControllers{false};	// m_options's, for the thread that runs the G1
		std::atomic<bool> m_octaveShift{false};			// as m_selectedControllers
		std::atomic<bool> m_racing{false};				// booting faster than real time: its audio is kept from the card
		bool m_rawMidiBound = false;
		std::string m_romProblem;
		std::vector<uint8_t> m_rom;		// kept for a restart
		Options m_options;
		std::string m_flashPath;
		// G1_UPDATE=1: the G1 starts in its boot ROM's update mode (the OS length in the flash
		// blanked, as on a G1 with no OS), for Clavia's own updater on the PC Port. At the end
		// an OS that came in is kept as an image and used from then on (HostOptions::os).
		bool m_updateMode = false;
		void keepReceivedOs();
		std::thread m_thread;
		std::atomic<bool> m_quit{false};

		std::mutex m_statsMutex;
		Stats m_stats;
		std::atomic<uint64_t> m_pcIn{0}, m_pcOut{0}, m_midiIn{0}, m_midiOut{0};
		HealthProbe m_health;		// the emulator's thread, and DSP 3's for output()
		std::vector<uint64_t> m_lastFrames = std::vector<uint64_t>(g1::g_maxDsps, 0);
		double m_lastReportTime = 0;
		uint64_t m_lastReportCycles = 0;

		// WAV (G1_RECORD)
		std::unique_ptr<std::ofstream> m_wav;
		std::string m_wavPath;
		uint64_t m_wavFrames = 0, m_wavMaxFrames = 0;
	};
}
