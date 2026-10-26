#pragma once

// The settings, on a card over the panel (CardView): which ROM the G1 runs, which audio driver and
// device it uses, its output level, whether its outputs 1/2 connect themselves to the sound card,
// which snd-virmidi card it takes over for programs that read raw MIDI devices
// (docs/bitwig-midi.md), the manual MIDI device pairing used as a patch on Windows while it cannot
// own ports (docs/windows-build.md), and the notice about Clavia, ROMs and support, which lives
// here instead of stopping every startup.
//
// It writes EmuHost::Options to the settings file next to the flash, which the console front end
// reads too. Only the level takes effect while it plays: everything else opens a driver, and the
// audio callback runs on the DSP thread, so it is applied on the next start and the card says so.
// The G1_* environment variables win over the file, and the card says that too.

#include "emuhost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace g1gui
{
	// A window's native title bar in the system's light or dark, following it when it changes.
	// Windows draws it light unless the window asks for dark (Windows 10 2004 and later); elsewhere
	// the system does this by itself and this does nothing. Made once the window is on the desktop
	// and before it is shown, or the bar shows light for a moment first.
	class NativeTitleBarTheme : private juce::DarkModeSettingListener
	{
	public:
		explicit NativeTitleBarTheme(juce::Component& _window);
		~NativeTitleBarTheme() override;
	private:
		void darkModeSettingChanged() override { apply(); }
		void apply();
		juce::Component& m_window;
	};

	// Sized by itself, in two columns (the ROM and audio, the MIDI ports), what is in use and the
	// notice below; the card's Close goes in its bottom row's right corner (CardView::CloseSpace).
	class SettingsView : public juce::Component, private juce::Timer
	{
	public:
		// _restart switches the G1 off and on (the window's own restart: this view goes with it).
		SettingsView(g1app::EmuHost& _host, std::function<void()> _restart);
		~SettingsView() override;
		void paint(juce::Graphics& _g) override;
		void resized() override;

	private:
		// The selections and the output level's value in a smaller font (TitleFontHeight for the
		// rows' titles), the controls at their size.
		class Look : public juce::LookAndFeel_V4
		{
		public:
			static constexpr float Smaller = 2.0f;
			static constexpr float TitleFontHeight = 15.0f - Smaller;	// a label's own is 15
			juce::Font getComboBoxFont(juce::ComboBox& _box) override;
			juce::Label* createSliderTextBox(juce::Slider& _slider) override;
		};

		void timerCallback() override;
		void apply();			// takes what the controls say into the options and saves them
		void chooseRom();		// the file picker, which checks the file before writing it down
		void chooseMicroOs();	// the same for the Micro Modular's OS (Clavia's updater or an image)
		void showMicroOs();		// which Micro OS the next start would use, or what is missing
		void updateEnabled();	// the ALSA device only matters with the ALSA driver, and so on
		void updateAudioDevices(const juce::String& selected = {});

		g1app::EmuHost& m_host;
		Look m_look;	// before the controls, so it outlives them

		juce::Label m_romLabel, m_audioLabel, m_deviceLabel, m_gainLabel, m_rawLabel, m_modelLabel, m_microOsLabel;
		juce::Label m_romPath, m_microOsPath;
		juce::TextButton m_microOsChoose{"Choose..."};
		juce::TextButton m_romChoose{"Choose..."}, m_romFolder{"Open the ROM folder"};
		std::unique_ptr<juce::FileChooser> m_chooser;
		juce::ComboBox m_model;		// Regular, Expanded or Micro Modular
		juce::ComboBox m_audio;
		juce::ComboBox m_device;
		juce::StringArray m_audioDevices;
		juce::Label m_midiInfo;
		// The manual MIDI pairing patch (Windows, until Windows MIDI Services can own ports):
		// pick an existing system device per direction instead of creating an owned port.
		juce::Label m_pcOutLabel, m_pcInLabel, m_midiOutLabel, m_midiInLabel;
		juce::ComboBox m_pcOutDevice, m_pcInDevice, m_midiOutDeviceBox, m_midiInDeviceBox;
		juce::Slider m_gain;
		juce::ToggleButton m_jackConnect{"Connect outputs 1/2 to the sound card"};
		juce::ToggleButton m_rawEnabled{"Take over a card, so raw MIDI programs see the G1"};
		juce::TextEditor m_rawCard;
		juce::ToggleButton m_disclaimer{"Show the notice at startup"};
		juce::TextButton m_noticeToggle;	// shows the notice's text in place of the settings, and back
		juce::TextEditor m_notice;
		bool m_noticeOpen = false;
		void setNoticeOpen(bool _open);
		juce::Label m_running, m_note;
		// Starting over, each after a question, then restarted: empty banks, or banks restored
		// from a saved flash (the user's set aside in a file either way), and the ROM's own OS
		// instead of an updated one (os =).
		std::function<void()> m_restart;
		juce::TextButton m_banks{"Banks..."}, m_os{"OS..."};
		void chooseBanks();
		void askThenRestart(const juce::String& _title, const juce::String& _message, const juce::String& _yes, std::function<void()> _then);
	};
}
