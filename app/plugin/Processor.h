#pragma once

#include <limits>

// The G1 as a VST3 instrument: notes from the track, four outputs (1/2 and 3/4, like the back
// panel), two inputs, and the G1's user state (patches and synth settings, never the OS) inside
// the DAW project. The emulator runs on the Runner's thread, paced by the host's blocks.
//
// Creating an instance creates no audio client and no file: everything goes through the host. The
// one thing it creates is a virtual MIDI port, "G1-Emu PC Port" ("G1-Emu 2 PC Port" for the second
// instance, and so on), so an editor (Animatek NME) can reach this G1 as it reaches the hardware's
// PC Port. JUCE can make one on Linux and macOS; on Windows it cannot, and the instance says so.
// It lives as long as the instance, across engine swaps, so the editor stays connected. The ROM is found like the standalone finds it (romfinder.h); the only file this ever
// writes is the settings file, and only when the user picks a ROM by hand.
//
// A new instance, with nothing in the project yet, starts from a copy of the standalone's flash
// when there is one, so the banks and the patches in the slots are the ones already there. It is
// a copy: the standalone's file is never written from here.

#include "hostclock.h"
#include "hostgestures.h"
#include "runner.h"
#include "jucemidi.h"
#include "g1Lib/g1knobs.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <map>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace g1plugin
{
	// One of the 18 panel knobs as a host parameter. Its name says what the knob moves in the
	// patch now ("Knob 3: OscA Freq coars"), and its text is the value the OS gives that parameter,
	// both read from the OS's tables (g1knobs.h) and changed when another patch comes. The value
	// reads as the editor shows it ("Sine", "1.25 kHz": g1format.h).
	class KnobParameter final : public juce::AudioParameterFloat
	{
	public:
		explicit KnobParameter(int _index);
		juce::String getName(int _maxLength) const override;
		bool setInfo(const g1::KnobInfo& _info);		// true if the name changed

		// The parameter's 0..1 against the knob's position (ADC). Positions 0 and 255 change
		// nothing in the OS, so the parameter's range is 1..254: its ends are the parameter's ends.
		static float toParam(uint8_t _adc) { return juce::jlimit(0.0f, 1.0f, (static_cast<float>(_adc) - 1.0f) / 253.0f); }
		static uint8_t toAdc(float _v) { return static_cast<uint8_t>(1 + juce::roundToInt(juce::jlimit(0.0f, 1.0f, _v) * 253.0f)); }

	private:
		juce::String getText(float _v, int _maxLength) const override;
		float getValueForText(const juce::String& _text) const override;
		const int m_index;
		mutable std::mutex m_mutex;
		juce::String m_name;
		g1::KnobInfo m_info;
	};

	// The master volume as a host parameter, in the OS's 128 steps: it takes the knob's position
	// halved (NOTES.md, "Master volume").
	class VolumeParameter final : public juce::AudioParameterInt
	{
	public:
		VolumeParameter() : juce::AudioParameterInt(juce::ParameterID{"volume", 1}, "Master Volume", 0, 127, 127) {}
		static int fromAdc(const int _adc) { return _adc / 2; }
		static uint8_t toAdc(const int _v) { return static_cast<uint8_t>(juce::jlimit(0, 127, _v) * 2 + 1); }
		void setNotifyingHost(const int _v) { setValueNotifyingHost(convertTo0to1(static_cast<float>(_v))); }
	};

	// The output gain (issue #65): the standalone's "Output level" slider, missing from the plugin,
	// which had no way at all to make up for a quiet patch beyond the host's own fader. A new
	// instance starts at 48 dB (DefaultDb): +36 dB only undoes the OS's -36 dB cap on the master
	// volume, and most patches still came out too quiet next to other instruments. A project saved
	// before this parameter existed ran at a fixed 36 dB and reads back 36 dB (readPreferences).
	class GainParameter final : public juce::AudioParameterFloat
	{
	public:
		static constexpr float DefaultDb = 48.0f;
		static constexpr float LegacyDb = 36.0f;		// the fixed gain before this parameter
		GainParameter() : juce::AudioParameterFloat(juce::ParameterID{"gain", 1}, "Output Gain",
			juce::NormalisableRange<float>(0.0f, 60.0f, 1.0f), DefaultDb, juce::AudioParameterFloatAttributes().withLabel("dB")) {}
	};

	// A slot's octave shift (-2 to +2), as the Oct keys step it and its patch keeps it (MidiRouting).
	class OctaveParameter final : public juce::AudioParameterInt
	{
	public:
		explicit OctaveParameter(const int _slot)
			: juce::AudioParameterInt(juce::ParameterID{juce::String("octave") + static_cast<char>('A' + _slot), 1},
				juce::String("Slot ") + static_cast<char>('A' + _slot) + " Octave", -g1app::MidiRouting::MaxShift, g1app::MidiRouting::MaxShift, 0) {}
	};

	class Processor : public juce::AudioProcessor, private juce::AsyncUpdater, private juce::Timer
	{
	public:
		Processor();
		~Processor() override;

		void prepareToPlay(double _rate, int _maxBlock) override;
		void releaseResources() override;
		bool isBusesLayoutSupported(const BusesLayout& _layouts) const override;
		void processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi) override;
		using juce::AudioProcessor::processBlock;

		juce::AudioProcessorEditor* createEditor() override;
		bool hasEditor() const override { return true; }

		const juce::String getName() const override { return JucePlugin_Name; }	// "G1-Emu" or "G1-Emu FX"
		bool acceptsMidi() const override { return true; }
		bool producesMidi() const override { return true; }	// the G1's MIDI OUT
		bool isMidiEffect() const override { return false; }
		// A G1 sounds without notes (an LFO, a drone, a sequencer), so its tail is endless: a host
		// that reads 0 puts the plugin to sleep while nothing comes in (Bitwig), the emulation stops
		// with it, and an editor's upload stops halfway, leaving the patch half in (found with the
		// Micro, 2026-10-27). Infinite is VST3's kInfiniteTail through JUCE. The CLAP wrapper (which
		// leaves wrapperType alone) multiplies it by the rate into an int, which infinity overflows,
		// and an AU hands it to the system as it is: those get about three hours, the most that fits
		// at 192 kHz, endless all the same.
		double getTailLengthSeconds() const override
		{
			if(wrapperType == wrapperType_VST3)
				return std::numeric_limits<double>::infinity();
			return (std::numeric_limits<int32_t>::max() - 192000.0) / 192000.0;
		}

		// VST3 carries no Program Change as MIDI: a host that has one for the plugin sets its
		// program parameter instead. These 128 programs are Program Changes on channel 1 (slot A's
		// channel by default), so a Program Change from the track reaches the G1 either way.
		int getNumPrograms() override { return 128; }
		int getCurrentProgram() override { return m_programs[0].program.load() < 0 ? 0 : m_programs[0].program.load(); }
		void setCurrentProgram(int _index) override;
		const juce::String getProgramName(int _index) override { return "Program " + juce::String(_index + 1); }
		void changeProgramName(int, const juce::String&) override {}

		void getStateInformation(juce::MemoryBlock& _dest) override;
		void setStateInformation(const void* _data, int _size) override;

		// For the editor, on the message thread. The engine can be replaced (a project's state
		// arriving reboots the G1 with its flash): generation() changes when it is, and the
		// editor drops its panel before the old one goes (Editor::engineGoing).
		g1app::Engine* engine();
		int generation() const { return m_generation.load(); }
		g1app::HostStats stats();
		const std::string& romProblem() const { return m_romProblem; }
		const std::string& romPath() const { return m_romPath; }
		std::string describe();		// for the Settings box: ROM, latency, where the banks came from

		// The ROM picked by hand: remembered in the settings file, like the standalone does.
		void useRom(const juce::File& _file);

		// Switches the G1 off and on (the panel's Restart): its state as it is now goes back in as a
		// project's would, so a new G1 boots with the same banks, slots, knobs and programs.
		// Message thread; the editor's panel goes with the old G1.
		void restart();

		// The panel's preferences, kept in the project. A change is told to the host, or most would
		// not save a project where nothing else changed and lose it (message thread). They are also
		// what a new instance starts with (the standalone's settings.conf, the size alone in
		// plugin.conf): the switches are written at once, the size when the editor closes.
		g1app::SynthSettingsLink& synthSettings() { return m_synthSettings; }
		g1app::PresetsLink& presets() { return m_presets; }

		// The Settings card's gain slider (issue #65): the host's own automatable parameter, the
		// same one processBlock reads, so the slider and host automation never fight each other.
		juce::RangedAudioParameter& gainParameter() { return *m_gainParam; }
		bool lastController(int& _channel, int& _cc, uint32_t& _serial);	// for MIDI Learn: the runner's, while there is one
		// MIDI CC Mode on Selected Slots (MidiRouting): the project's, not a new instance's preference.
		bool selectedControllers() const { return m_selectedControllers; }
		void setSelectedControllers(bool _on);
		// Octave Shift On (MidiRouting): the notes moved by their slot's shift. The project's too.
		bool octaveShift() const { return m_octaveShift; }
		void setOctaveShift(bool _on);
		bool extrasOpen() const { return m_extrasOpen; }
		void setExtrasOpen(bool _open) { if(std::exchange(m_extrasOpen, _open) != _open) { stateChanged(); savePreferences(); } }
		bool knobDisplays() const { return m_knobDisplays; }
		void setKnobDisplays(bool _on) { if(std::exchange(m_knobDisplays, _on) != _on) { stateChanged(); savePreferences(); } }
		bool knobFollowsPatch() const { return m_knobFollowsPatch; }
		void setKnobFollowsPatch(bool _on) { if(std::exchange(m_knobFollowsPatch, _on) != _on) { stateChanged(); savePreferences(); } }
		uint32_t randomExcluded() const { return m_randomExclude; }
		void setRandomExcluded(uint32_t _knobs) { if(std::exchange(m_randomExclude, _knobs) != _knobs) { stateChanged(); savePreferences(); } }
		float panelScale() const { return m_panelScale; }
		void setPanelScale(float _scale) { if(std::abs(std::exchange(m_panelScale, _scale) - _scale) > 0.002f) stateChanged(); }
		void savePreferences() const;
		// Regular (4 DSPs), Expanded (8, the expansion board) or Micro Modular (1 DSP, its own OS): the
		// project's, and a new instance takes the last one chosen. A change restarts the G1, which
		// keeps what restart() keeps. On the message thread, and not from inside the panel, which the
		// restart replaces.
		enum class Model { Regular, Expanded, Micro };
		Model model() const { return m_model; }
		void setModel(Model _m);
		// What happened to the Micro's OS when this instance was made: which one runs, or why the
		// instance runs as a Regular instead. Empty on the other models.
		std::string microNote();

	private:
		void stateChanged() { updateHostDisplay(juce::AudioProcessorListener::ChangeDetails().withNonParameterStateChanged(true)); }
		void loadPreferences();
		void handleAsyncUpdate() override;

		// The 18 knobs and the master volume both ways: the host's automation turns them
		// (processBlock), and what turns them otherwise (the panel, Random, a project's state) is
		// passed to the host (the timer).
		// m_knobMutex keeps the two apart; processBlock only tries it, and if the timer has it,
		// leaves the knobs for the next block. The host is told outside every lock, as gestures
		// (HostGestures, #42), and a knob on its way to the host (m_toHost) is left alone by
		// processBlock until the host has its value.
		void timerCallback() override;
		void knobsFromHost();
		void knobsFromEngine();
		std::array<KnobParameter*, 18> m_knobParams{};
		std::array<int, 18> m_lastAdc{};				// the position each knob was last seen at
		std::array<float, 18> m_lastParam{};			// the value each parameter was last seen at
		VolumeParameter* m_volumeParam = nullptr;
		// The slots' octave shifts, both ways: the host's changes go to the G1, the Oct keys' to the host
		// (octavesToAndFromHost). What each was when they last agreed; under m_lifecycle.
		std::array<OctaveParameter*, 4> m_octaveParams{};
		std::array<int, 4> m_lastOctaveParams{};
		int m_lastVolumeAdc = -1, m_lastVolumeParam = -1;
		GainParameter* m_gainParam = nullptr;
		float m_lastGainDb = GainParameter::DefaultDb;		// audio thread only: the value last given to the runner
		std::mutex m_knobMutex;
		int m_knobGeneration = -1;
		static constexpr size_t VolumeIndex = 18;		// after the knobs, in m_gestures and m_toHost
		HostGestures<19> m_gestures;					// message thread only
		std::array<std::atomic<bool>, 19> m_toHost{};
		void findRom();
		// Makes the engine from the state (or, with none, from the standalone's flash) and starts
		// it if the host is playing. Message thread, or before anything runs.
		void createEngine(const juce::MemoryBlock* _state);
		void destroyEngine();
		// Under m_lifecycle. The runner's latency, or -1 with none started: the caller gives it to
		// the host with setLatencySamples once the lock is released, as a Steinberg host answers a
		// latency change at once, by calling releaseResources and prepareToPlay from inside it (#66).
		int startRunner();
		void reportLatency(int _latency);
		bool applyState(g1app::Engine& _engine, const juce::MemoryBlock& _state);
		juce::MemoryBlock snapshotState();
		juce::MemoryBlock settingsOnlyState();
		juce::XmlElement stateXml() const;					// the tag, the panel's preferences and the programs
		void readPreferences(const juce::XmlElement& _xml);	// the panel's preferences out of a state
		// The knobs something other than the host turned: (index, value) for the host, VolumeIndex
		// for the volume. Under m_lifecycle; the host is told after (tellHost).
		void knobsToHost(g1::Microcontroller& _mc, std::vector<std::pair<size_t, float>>& _edits);
		// Under m_lifecycle: the host's octave changes written to the G1, and the G1's, in _edits, for
		// tellHostOctaves() to pass on once no lock is held.
		void octavesToAndFromHost(g1::Microcontroller& _mc, std::vector<std::pair<size_t, int>>& _edits);
		void tellHostOctaves(const std::vector<std::pair<size_t, int>>& _edits);
		void restoreStep();			// m_lifecycle held
		void writePendingSettings();	// m_lifecycle held
		void tellHost(const std::vector<std::pair<size_t, float>>& _edits);
		void startFromStandalone(g1app::Engine& _engine);

		// The PC Port's virtual MIDI port. Declared before the engine and the runner so it goes after them.
		std::unique_ptr<juce::InterProcessLock> m_instanceLock;	// holds m_instance's number for every process
		int m_instance = 0;						// 1 for the first live instance, 2 for the next...
		std::unique_ptr<g1app::JuceMidi> m_pcPort;
		int m_pcIndex = -1;
		std::string m_pcProblem;				// why there is no PC Port, when there is none
		void openPcPort();
		// The direct link to Animatek NME (#8): the PC Port over a local socket. It needs no MIDI
		// endpoint at all, so it is there where JUCE can make no virtual port (Windows) and in a
		// CLAP instance that came after JUCE's MIDI shut down.
		g1app::DirectLink m_link;
		std::string m_linkProblem;
		void openLink();

		std::vector<uint8_t> m_rom;
		std::string m_romPath, m_romProblem;
		// A model change between the Micro and a rack leaves the old G1 here, whole, for when it comes back
		std::map<Model, juce::MemoryBlock> m_parked;

		std::mutex m_lifecycle;					// m_engine and m_runner are swapped under it
		std::unique_ptr<g1app::Engine> m_engine;
		std::unique_ptr<g1app::Runner> m_runner;
		std::unique_ptr<g1app::SlotKeeper> m_keeper;	// what each slot holds; lives as long as m_engine
		g1app::SynthSettingsLink m_synthSettings;	// the OS's synth settings, for the panel's overlay
		// What a project keeps besides the flash and the slots (#46). m_lifecycle guards all of it
		// but m_restoring, which processBlock reads.
		static constexpr int SettingsTries = 5;
		// After a write, how long to wait for it to be read back: the settings, SlotsSelected and
		// SlotActivated take 300 ms of G1 time each before the reading (1500 ms was too short: written
		// again and again, never taken).
		static constexpr juce::uint32 SettingsCheckMs = 3000;
		static constexpr juce::uint32 PollMs = 2000;			// how often the OS's settings are read for the project
		static constexpr juce::uint32 RestoreHoldMs = 15000;	// the longest the notes are held back
		static constexpr uint32_t SystemLedRow = 3, SystemLedBit = 4;	// the System key's LED
		// The project's synth settings, written once the keeper has put the slots back (a write in
		// the middle of a slot's upload breaks it: the OS shows "Error"). Kept until the OS reads
		// them back, and written again up to SettingsTries times if it does not.
		std::optional<g1app::SynthSettings> m_pendingSettings;
		int m_settingsTries = 0;
		juce::uint32 m_settingsWrittenAt = 0;	// 0: not written yet (or to be written again)
		uint64_t m_settingsRevision = 0;		// the link's revision before the write: a later one is its reading back
		// The slots' octave shifts as the restored patches left them, taken before the write: the
		// SlotActivated it sends can set the active slot's back to 0, so they go back in after it.
		std::optional<std::array<int, 4>> m_restoredShifts;
		juce::uint32 m_polledAt = 0;			// when the OS's settings were last asked for
		bool m_systemMenu = false;				// the System key's LED was lit at the last look
		std::array<int, 4> m_octaves{};		// each slot's octave shift at the last look
		// A project's state going back in (slots, then synth settings): the track's notes are left
		// out meanwhile, so none is played on a slot whose MIDI channel is about to change and then
		// hangs, its note off arriving on the channel the slot no longer hears. At most
		// RestoreHoldMs, in case the keeper never settles.
		std::atomic<bool> m_restoring{false};
		// While restoring, the MIDI channels whose notes may come in all the same
		// (bit per channel): each slot on them is back and keeps its channel (restoreStep).
		std::atomic<uint32_t> m_openChannels{0};
		std::atomic<bool> m_holdAll{false};	// restoring with every channel held: the runner races
		juce::uint32 m_restoreStart = 0;
		g1app::PresetsLink m_presets;				// the OS's banks, for the panel's Presets page
		std::vector<uint8_t> m_os;				// HostOptions::os, if set: the OS to run instead of the ROM's
		std::atomic<int> m_generation{0};
		std::string m_origin;					// where this instance's flash came from

		// A state the host gave before there was an engine (or with no ROM at all): it is what
		// gets used, and what gets handed back, so a project opened without the ROM loses nothing.
		juce::MemoryBlock m_state;
		bool m_haveState = false;
		bool m_keepProjectState = false;		// the project's G1 state could not be used: hand it back as it came
		std::mutex m_pendingMutex;
		juce::MemoryBlock m_pending;			// set off the message thread, applied on it

		double m_rate = 0;
		int m_maxBlock = 0;
		bool m_prepared = false;
		juce::AudioBuffer<float> m_inputs;		// the inputs, copied before the outputs overwrite them

		bool m_extrasOpen = false, m_knobDisplays = false, m_knobFollowsPatch = false;
		Model m_model = Model::Regular;
		std::string m_microNote;			// m_lifecycle: the Micro's OS in use, or why there is none
		bool m_microFallback = false;		// the model is Micro and this instance runs as a Regular for want of its OS
		bool m_selectedControllers = false;		// MIDI CC Mode on Selected Slots: given to each runner
		bool m_octaveShift = false;				// Octave Shift On: given to each runner
		uint32_t m_randomExclude = 0;			// the knobs Random leaves alone: bit k for knob k + 1
		float m_panelScale = 1.25f;				// the editor's size: 1 is the panel's 1200 pixels wide; 1.25 is half the skin's

		// The last Bank Select (CC 0 and 32) and Program Change the track sent on each channel.
		// The OS does not keep which patch each slot had, so a G1 booting from a saved project
		// would come up with empty slots: these are sent again as soon as it starts. -1: none.
		struct Program { std::atomic<int> bankMsb{-1}, bankLsb{-1}, program{-1}; };
		std::array<Program, 16> m_programs;
		bool m_unstarted = false;				// a new instance's G1 that has not run yet (settingsOnlyState)
		bool m_engineFresh = false;				// created and not started yet: replay the programs
		std::atomic<int> m_hostProgram{-1};		// set by the host's program parameter, sent by processBlock
		// The host's transport as MIDI clock for the G1 (issue #20); audio thread only.
		g1app::HostClock m_hostClock;
		g1app::HostClock::Block m_clockBlock;
		void replayPrograms();
		std::string programsToString() const;
		void programsFromString(const juce::String& _text);
	};
}
