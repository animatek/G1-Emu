#pragma once

// The settings window: which ROM the G1 runs, which audio driver and device it uses, its output
// level, whether its
// outputs 1/2 connect themselves to the sound card, which snd-virmidi card it takes over for
// programs that read raw MIDI devices (docs/bitwig-midi.md), and the notice about Clavia, ROMs
// and support, which lives here instead of stopping every startup.
//
// It writes EmuHost::Options to the settings file next to the flash, which the console front end
// reads too. Only the level takes effect while it plays: everything else opens a driver, and the
// audio callback runs on the DSP thread, so it is applied on the next start and the window says
// so. The G1_* environment variables win over the file, and the window says that too.

#include "emuhost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace g1gui
{
	// The notice about Clavia, ROMs and support. It is the same text as the README's "Please read
	// this first", shown in the settings window and, the first time G1-Emu runs, at startup.
	const char* disclaimerText();

	class SettingsView : public juce::Component, private juce::Timer
	{
	public:
		explicit SettingsView(g1app::EmuHost& _host);
		void paint(juce::Graphics& _g) override;
		void resized() override;

		// Opens it (or brings it to the front) as a window of its own.
		static void show(g1app::EmuHost& _host, juce::Component* _parent);

	private:
		void timerCallback() override;
		void apply();			// takes what the controls say into the options and saves them
		void chooseRom();		// the file picker, which checks the file before writing it down
		void updateEnabled();	// the ALSA device only matters with the ALSA driver, and so on

		g1app::EmuHost& m_host;

		juce::Label m_romLabel, m_audioLabel, m_deviceLabel, m_gainLabel, m_rawLabel;
		juce::Label m_romPath;
		juce::TextButton m_romChoose{"Choose..."}, m_romFolder{"Open the ROM folder"};
		std::unique_ptr<juce::FileChooser> m_chooser;
		juce::ComboBox m_audio;
		juce::TextEditor m_device;
		juce::Slider m_gain;
		juce::ToggleButton m_jackConnect{"Connect outputs 1/2 to the sound card"};
		juce::ToggleButton m_rawEnabled{"Take over a card, so raw MIDI programs see the G1"};
		juce::TextEditor m_rawCard;
		juce::ToggleButton m_disclaimer{"Show the notice below at startup"};
		juce::TextEditor m_notice;
		juce::Label m_running, m_note;
		juce::TextButton m_close{"Close"};
	};
}
