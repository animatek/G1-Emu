#pragma once

// What every front end around the engine shares and none of them owns: the per-user data
// directory, the settings file, and the figures a panel shows in its status bar. The console and
// the window (EmuHost) use all of it; the plugin reads the ROM path from the settings file and
// fills in the statistics, and never writes anything here on its own.

#include "g1Lib/g1mc.h"

#include <cstdint>
#include <string>
#include <vector>

namespace g1app
{
	// The per-user G1-Emu directory: %APPDATA%/Animatek/G1-Emu on Windows,
	// ~/.local/share/Animatek/G1-Emu elsewhere. The standalone's flash and settings live there.
	std::string defaultFlashPath();
	std::string defaultSettingsPath();

	// The knobs Random leaves alone, as the settings files write them: their numbers, 1-18,
	// separated by spaces ("1 7 18"). Anything else in the text is skipped.
	std::string knobListToString(uint32_t _knobs);
	uint32_t knobListFromString(const std::string& _text);

	// What the user gets to choose. The window sets them from its settings panel and g1run leaves
	// them at their defaults; EmuHost lets the G1_* variables win over both.
	struct HostOptions
	{
		std::string audio = "jack";			// "jack", "alsa", "no", or the name of an ALSA device
		float gainDb = 36.0f;				// undoes the -36 dB cap the OS puts on the master volume
		bool jackConnect = true;			// out_1/out_2 connect themselves to the sound card
		bool directLink = true;				// the PC Port over a local socket for Animatek NME (directlink.h)
		std::string rawMidiCard = "G1";		// the raw MIDI card to take over (empty: none)
		std::string rom;					// the ROM to use; empty: look for one (romfinder.h)
		// The ROM this run uses, never saved: one given for a run only (the command line, G1_ROM,
		// G1-Mods' modified ROM) is not the standalone's and the plugins' ROM, which rom keeps.
		std::string romInUse;
		std::string os;						// an OS image to run instead of the ROM's factory OS (empty: none)
		bool expanded = false;				// the model: Expanded (8 DSPs, the expansion board) or Regular (4)
		bool showDisclaimer = false;		// the notice at startup; the window can turn it back on
		bool extrasOpen = false;			// the window's extras drawer (Random...) was left open
		bool knobDisplays = false;			// a display above each knob with what it is assigned to
		bool knobFollowsPatch = false;		// the knobs show the patch's values, not where they were turned
		bool selectedControllers = false;	// MIDI CC Mode on Selected Slots: controllers go to every selected slot (MidiRouting)
		bool octaveShift = false;			// Octave Shift On: the notes moved by their slot's shift, and the panel's OCT SHIFT keys (MidiRouting); Off, as a rack
		uint32_t randomExclude = 0;			// the knobs Random leaves alone: bit k for knob k + 1
		bool tooltips = true;				// the window's tooltips beside the mouse
		bool presetsHideEmpty = true;		// the Presets page lists only the positions that hold a patch
		int masterVolume = -1;				// the window's master volume knob as it was left (0-255; -1: not yet)
		float panelScale = 1.25f;			// the window's size: 1 is the panel's 1200 pixels wide; 1.25 is half the skin's

		// Manual MIDI device pairing (JUCE backend only): the name of an existing system MIDI
		// device to open instead of creating an owned port, one per direction. Empty means try
		// to create an owned port as usual. This is the patch for Windows while Windows MIDI
		// Services cannot own ports (Microsoft/MIDI issue #1047, docs/windows-build.md): point
		// these at a loopback driver's ports (e.g. loopMIDI) and point the editor at the same
		// names. Ignored on the native (ALSA) backend, which always owns real sequencer ports.
		std::string pcPortOutDevice, pcPortInDevice;	// PC Port: the editor's side
		std::string midiOutDevice, midiInDevice;		// MIDI: the DAW/keyboard's side

		// The names used in the settings file and in the panel.
		static const char* const audioNames[3];	// "jack", "alsa", "no"

		// A plain "key = value" file next to the flash, so the window and the console agree
		// on what they use. Missing or unreadable, the defaults stand; load() says whether it
		// was there, which is how the window knows it is a first run.
		bool load(const std::string& _path);

		// The OS image named by `os` (or G1_OS, which wins), read into _image. Empty when there is
		// none or it cannot be read; _note says which was used, or why not, for the log.
		std::vector<uint8_t> loadOs(std::string& _note) const;
		bool save(const std::string& _path) const;

		// The DSPs of the model chosen (`model`, or G1_MODEL=regular|expanded, which wins): 4 or 8.
		uint32_t dspCount() const;
		static const char* modelName(uint32_t _dspCount) { return _dspCount > g1::g_regularDsps ? "Expanded" : "Regular"; }
	};

	// What a running G1 reports, for the status bar and the Report issue button.
	struct HostStats
	{
		double seconds = 0;			// time since start
		double speed = 0;			// % of real time
		double load = 0;			// % of the time the emulator thread is busy
		double cpuCores = 0;		// cores used by the process (the DSP threads spin while waiting)
		uint32_t dspCount = g1::g_regularDsps;	// the model's
		bool dspOn[g1::g_maxDsps] = {};
		bool dspFailed[g1::g_maxDsps] = {};	// stopped because the JIT could not generate its code (#17)
		std::string dspProblem;		// why, for the status bar and a report (empty: none)
		uint64_t pcIn = 0, pcOut = 0, midiIn = 0, midiOut = 0;
		float peak = 0;				// peak of outputs 1/2 (0-1) since the last query
		uint64_t xruns = 0;
		std::string audio;			// which audio output is in use
		std::string midi;			// the MIDI ports
		std::string rawMidi;		// the raw MIDI devices linked to them (empty: none)
	};
}
