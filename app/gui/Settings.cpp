#include "Settings.h"

#include "romfinder.h"
#ifdef G1_BACKEND_JUCE
#include "juceaudio.h"
#if defined(_WIN32) && !defined(G1_WINDOWS_MIDI)
#include "jucemidi.h"
#endif
#endif

namespace g1gui
{
	namespace
	{
		constexpr int g_labelW = 150, g_rowH = 28, g_gap = 10, g_margin = 18;
		// The manual MIDI pairing patch (Windows, until Windows MIDI Services can own ports;
		// jucemidi.h) adds four device rows to the MIDI section, so the window grows to fit them.
#if defined(_WIN32) && defined(G1_BACKEND_JUCE) && !defined(G1_WINDOWS_MIDI)
		constexpr bool g_manualMidiUi = true;
#else
		constexpr bool g_manualMidiUi = false;
#endif
		constexpr int g_midiInfoH = g_manualMidiUi ? 92 : 76;
		constexpr int g_midiRowsH = g_manualMidiUi ? 4 * (g_rowH + g_gap) : 0;
		constexpr int g_midiExtra = (g_midiInfoH - 76) + g_midiRowsH + (g_manualMidiUi ? 10 : 0);
		constexpr int g_width = 560, g_height = 790 + g_midiExtra;

		// The window holding one SettingsView. There is at most one, kept here so a second
		// click brings the same one to the front instead of opening another.
		class SettingsWindow : public juce::DocumentWindow
		{
		public:
			SettingsWindow(g1app::EmuHost& _host)
				: DocumentWindow("G1-Emu settings", juce::Colour(0xff1c1c20), DocumentWindow::closeButton)
			{
				setUsingNativeTitleBar(true);
				setContentOwned(new SettingsView(_host), true);
				setResizable(false, false);
				centreWithSize(getWidth(), getHeight());
				setVisible(true);
			}
			void closeButtonPressed() override { s_open.reset(); }

			static std::unique_ptr<SettingsWindow> s_open;
		};

		std::unique_ptr<SettingsWindow> SettingsWindow::s_open;
	}

	// The same text as the README's "Please read this first".
	const char* disclaimerText()
	{
		return
			"G1-Emu is an independent, open-source emulator of the Nord Modular G1.\n\n"
			"- It is not affiliated with, endorsed by or connected to Clavia DMI in any way. "
			"\"Nord\" and \"Nord Modular\" are trademarks of Clavia DMI.\n\n"
			"- No ROMs or firmware are included, and none will ever be provided. Please do not ask "
			"for them: you will not find them here.\n\n"
			"- There is no support. This is a pre-alpha community project, made in spare time. Bug "
			"reports and contributions are welcome on GitHub; requests for help, ROMs or builds are not.";
	}

	void SettingsView::show(g1app::EmuHost& _host, juce::Component*)
	{
		if(SettingsWindow::s_open)
			SettingsWindow::s_open->toFront(true);
		else
			SettingsWindow::s_open = std::make_unique<SettingsWindow>(_host);
	}

	SettingsView::SettingsView(g1app::EmuHost& _host) : m_host(_host)
	{
		const auto& o = m_host.options();

		auto label = [this](juce::Label& _l, const juce::String& _text)
		{
			_l.setText(_text, juce::dontSendNotification);
			_l.setColour(juce::Label::textColourId, juce::Colours::white);
			_l.setJustificationType(juce::Justification::centredRight);
			addAndMakeVisible(_l);
		};
		label(m_romLabel, "ROM in use");
		label(m_audioLabel, "Audio driver");
		label(m_deviceLabel, "Audio device");
		label(m_gainLabel, "Output level");
		label(m_rawLabel, "Card ID");

		// The ROM is the user's and always will be: this says which one is running and lets them
		// point at another. A change needs a restart, like any other ROM swap would on hardware.
		m_romPath.setColour(juce::Label::textColourId, juce::Colour(0xffc8ccd0));
		m_romPath.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
		m_romPath.setText(o.rom.empty() ? "none" : o.rom, juce::dontSendNotification);
		m_romPath.setTooltip(o.rom);
		addAndMakeVisible(m_romPath);

		m_romChoose.onClick = [this] { chooseRom(); };
		addAndMakeVisible(m_romChoose);
		m_romFolder.setTooltip(g1app::publicRomFolder());
		m_romFolder.onClick = []
		{
			const juce::File folder(g1app::publicRomFolder());
			(void)folder.createDirectory();
			folder.revealToUser();
		};
		addAndMakeVisible(m_romFolder);

		// The native Linux backend is a JACK node or a plain ALSA device. Everywhere else JUCE
		// exposes the system's real CoreAudio/WASAPI/DirectSound devices.
#ifdef G1_BACKEND_JUCE
		m_audio.addItem("System default", 1);
		m_audio.addItem("None  (no sound)", 3);
		for(const auto& device : g1app::JuceAudio::devices())
		{
			m_audioDevices.add(device);
			const auto type = juce::String(device).upToFirstOccurrenceOf(": ", false, false);
			bool found = false;
			for(int i = 0; i < m_audio.getNumItems(); ++i)
				found |= m_audio.getItemText(i) == type;
			if(!found) m_audio.addItem(type, m_audio.getNumItems() + 10);
		}
		const auto requestedType = juce::String(o.audio).upToFirstOccurrenceOf(": ", false, false);
		m_audio.setSelectedId(o.audio == "no" ? 3 : 1, juce::dontSendNotification);
		for(int i = 0; i < m_audio.getNumItems(); ++i)
			if(m_audio.getItemText(i) == requestedType)
				m_audio.setSelectedItemIndex(i, juce::dontSendNotification);
#else
		m_audio.addItem("JACK / PipeWire  (4 outputs, 2 inputs)", 1);
		m_audio.addItem("ALSA  (outputs 1/2 only)", 2);
		m_audio.addItem("None  (no sound)", 3);
		m_audio.setSelectedId(o.audio == "no" ? 3 : (o.audio == "jack" ? 1 : 2), juce::dontSendNotification);
#endif
		m_audio.onChange = [this] { updateAudioDevices(); updateEnabled(); apply(); };
		addAndMakeVisible(m_audio);

		m_device.addItem("Default", 1);
#ifdef G1_BACKEND_JUCE
		updateAudioDevices(juce::String(o.audio).fromFirstOccurrenceOf(": ", false, false));
		m_device.setTooltip("Audio device and driver to open on the next start");
#else
		m_device.setEditableText(true);
		m_device.setText(o.audio == "jack" || o.audio == "no" || o.audio == "alsa" ? "Default" : o.audio,
			juce::dontSendNotification);
		m_device.setTooltip("The ALSA device to open: default, hw:0,0, ...");
#endif
		m_device.onChange = [this] { apply(); };
		addAndMakeVisible(m_device);

		// The only one that takes effect straight away: both backends read it from an atomic.
		m_gain.setSliderStyle(juce::Slider::LinearHorizontal);
		m_gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
		m_gain.setRange(0.0, 60.0, 1.0);
		m_gain.setTextValueSuffix(" dB");
		m_gain.setValue(o.gainDb, juce::dontSendNotification);
		m_gain.setTooltip("The OS caps the G1's master volume at -36 dB; +36 dB undoes it");
		m_gain.onValueChange = [this] { m_host.setGainDb(static_cast<float>(m_gain.getValue())); apply(); };
		addAndMakeVisible(m_gain);

		m_jackConnect.setToggleState(o.jackConnect, juce::dontSendNotification);
		m_jackConnect.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
		m_jackConnect.onClick = [this] { apply(); };
		addAndMakeVisible(m_jackConnect);

		m_rawEnabled.setToggleState(!o.rawMidiCard.empty(), juce::dontSendNotification);
		m_rawEnabled.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
		m_rawEnabled.setTooltip("Bitwig on Linux reads raw MIDI devices and never sees an ALSA sequencer port; see docs/bitwig-midi.md");
		m_rawEnabled.onClick = [this] { updateEnabled(); apply(); };
		addAndMakeVisible(m_rawEnabled);

		m_rawCard.setText(o.rawMidiCard.empty() ? "G1Emu" : o.rawMidiCard, false);
		m_rawCard.onFocusLost = [this] { apply(); };
		m_rawCard.onReturnKey = [this] { apply(); };
		addAndMakeVisible(m_rawCard);
#ifdef G1_BACKEND_JUCE
		m_jackConnect.setVisible(false);
		m_rawEnabled.setVisible(false);
		m_rawCard.setVisible(false);
		m_rawLabel.setVisible(false);
		m_midiInfo.setColour(juce::Label::textColourId, juce::Colour(0xffc8ccd0));
		m_midiInfo.setJustificationType(juce::Justification::topLeft);
		m_midiInfo.setText("PC Port: connect the editor here (input and output).\n"
			"MIDI: connect the DAW or keyboard here.\n"
#if defined(_WIN32) && !defined(G1_WINDOWS_MIDI)
			"Windows cannot create these as owned ports yet (Microsoft/MIDI issue #1047): pick a "
			"loopback driver's ports below (e.g. loopMIDI) and pick the same ones in the editor.",
			juce::dontSendNotification);
#else
			"Port creation is automatic; check the running status below.", juce::dontSendNotification);
#endif
		addAndMakeVisible(m_midiInfo);
#if defined(_WIN32) && !defined(G1_WINDOWS_MIDI)
		{
			// Matches a saved name against what the system has right now, keeping it selected
			// (with a "(not found)" marker) even if the loopback driver is not running yet.
			auto populate = [](juce::ComboBox& _box, const std::vector<std::string>& _devices,
				const std::string& _selected)
			{
				_box.clear(juce::dontSendNotification);
				_box.addItem("Automatic (owned port)", 1);
				int id = 2;
				for(const auto& d : _devices)
					_box.addItem(d, id++);
				if(_selected.empty())
				{
					_box.setSelectedId(1, juce::dontSendNotification);
					return;
				}
				for(int i = 0; i < _box.getNumItems(); ++i)
					if(_box.getItemText(i).toStdString() == _selected)
					{
						_box.setSelectedItemIndex(i, juce::dontSendNotification);
						return;
					}
				_box.addItem(_selected + "  (not found)", id);
				_box.setSelectedId(id, juce::dontSendNotification);
			};
			const auto outputs = g1app::JuceMidi::availableOutputs();
			const auto inputs = g1app::JuceMidi::availableInputs();
			populate(m_pcOutDevice, outputs, o.pcPortOutDevice);
			populate(m_pcInDevice, inputs, o.pcPortInDevice);
			populate(m_midiOutDeviceBox, outputs, o.midiOutDevice);
			populate(m_midiInDeviceBox, inputs, o.midiInDevice);

			auto label = [this](juce::Label& _l, const juce::String& _text)
			{
				_l.setText(_text, juce::dontSendNotification);
				_l.setColour(juce::Label::textColourId, juce::Colours::white);
				_l.setJustificationType(juce::Justification::centredRight);
				addAndMakeVisible(_l);
			};
			label(m_pcOutLabel, "PC Port out");
			label(m_pcInLabel, "PC Port in");
			label(m_midiOutLabel, "MIDI out");
			label(m_midiInLabel, "MIDI in");
			for(auto* box : {&m_pcOutDevice, &m_pcInDevice, &m_midiOutDeviceBox, &m_midiInDeviceBox})
			{
				box->onChange = [this] { apply(); };
				addAndMakeVisible(*box);
			}
			m_pcOutDevice.setTooltip("What G1-Emu sends the editor through, when not automatic");
			m_pcInDevice.setTooltip("What G1-Emu receives from the editor through, when not automatic");
			m_midiOutDeviceBox.setTooltip("What G1-Emu sends the DAW/keyboard through, when not automatic");
			m_midiInDeviceBox.setTooltip("What G1-Emu receives from the DAW/keyboard through, when not automatic");
		}
#endif
#endif

		// The notice used to stop every startup. It lives here now, readable at any time.
		m_disclaimer.setToggleState(o.showDisclaimer, juce::dontSendNotification);
		m_disclaimer.setColour(juce::ToggleButton::textColourId, juce::Colours::white);
		m_disclaimer.onClick = [this] { apply(); };
		addAndMakeVisible(m_disclaimer);

		m_notice.setMultiLine(true, true);
		m_notice.setReadOnly(true);
		m_notice.setScrollbarsShown(true);
		m_notice.setCaretVisible(false);
		m_notice.setFont(juce::FontOptions(12.0f));
		m_notice.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff16161a));
		m_notice.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff35353c));
		m_notice.setColour(juce::TextEditor::textColourId, juce::Colour(0xffc8ccd0));
		m_notice.setText(disclaimerText(), false);
		addAndMakeVisible(m_notice);

		for(auto* l : {&m_running, &m_note})
		{
			l->setColour(juce::Label::textColourId, juce::Colour(0xff9aa0a6));
			l->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
			l->setJustificationType(juce::Justification::topLeft);
			addAndMakeVisible(*l);
		}
		m_note.setText("The level applies now; the rest, on the next start.\n"
			"Environment variables win over this window.\n"
			"Saved in " + juce::String(g1app::EmuHost::defaultSettingsPath()), juce::dontSendNotification);

		m_close.onClick = [] { SettingsWindow::s_open.reset(); };
		addAndMakeVisible(m_close);

		updateEnabled();
		timerCallback();
		startTimerHz(4);
		setSize(g_width, g_height);
	}

	// Picking a ROM only writes it down: the emulator is already running on the old one, and
	// swapping it under a booted OS is not something the hardware does either.
	void SettingsView::chooseRom()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Choose the Nord Modular rack ROM",
			juce::File(m_host.options().rom).existsAsFile() ? juce::File(m_host.options().rom).getParentDirectory()
				: juce::File(g1app::publicRomFolder()), "*.bin;*.BIN");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this](const juce::FileChooser& _c)
		{
			const auto file = _c.getResult();
			if(file == juce::File())
				return;
			std::vector<uint8_t> data;
			const auto check = g1app::inspectRom(file.getFullPathName().toStdString(), data);
			if(!check.ok())
			{
				juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Not the ROM G1-Emu needs",
					file.getFullPathName() + "\n\n" + juce::String(check.what()));
				return;
			}
			m_host.options().rom = file.getFullPathName().toStdString();
			m_romPath.setText(file.getFullPathName(), juce::dontSendNotification);
			m_romPath.setTooltip(file.getFullPathName());
			apply();
		});
	}

	void SettingsView::updateAudioDevices(const juce::String& selected)
	{
#ifdef G1_BACKEND_JUCE
		m_device.clear(juce::dontSendNotification);
		if(m_audio.getSelectedId() == 1 || m_audio.getSelectedId() == 3)
			m_device.addItem("Default", 1);
		else
			for(const auto& device : m_audioDevices)
				if(device.startsWith(m_audio.getText() + ": "))
					m_device.addItem(device.fromFirstOccurrenceOf(": ", false, false), m_device.getNumItems() + 1);
		m_device.setSelectedItemIndex(0, juce::dontSendNotification);
		for(int i = 0; i < m_device.getNumItems(); ++i)
			if(m_device.getItemText(i) == selected)
				m_device.setSelectedItemIndex(i, juce::dontSendNotification);
#else
		(void)selected;
#endif
	}

	void SettingsView::updateEnabled()
	{
#ifdef G1_BACKEND_JUCE
		const bool audio = m_audio.getSelectedId() != 3;
		m_device.setEnabled(audio && m_audio.getSelectedId() != 1);
		m_deviceLabel.setEnabled(audio && m_audio.getSelectedId() != 1);
		m_jackConnect.setEnabled(false);
#else
		const bool alsa = m_audio.getSelectedId() == 2;
		const bool jack = m_audio.getSelectedId() == 1;
		m_device.setEnabled(alsa);
		m_deviceLabel.setEnabled(alsa);
		m_jackConnect.setEnabled(jack);
#endif
		m_gain.setEnabled(m_audio.getSelectedId() != 3);
		m_gainLabel.setEnabled(m_audio.getSelectedId() != 3);
		m_rawCard.setEnabled(m_rawEnabled.getToggleState());
		m_rawLabel.setEnabled(m_rawEnabled.getToggleState());
	}

	void SettingsView::apply()
	{
		auto& o = m_host.options();
#ifdef G1_BACKEND_JUCE
		o.audio = m_audio.getSelectedId() == 3 ? "no" : m_audio.getSelectedId() == 1 ? "alsa"
			: (m_audio.getText() + ": " + m_device.getText()).toStdString();
#else
		switch(m_audio.getSelectedId())
		{
		case 1:  o.audio = "jack"; break;
		case 3:  o.audio = "no"; break;
		default: o.audio = m_device.getText().trim().isEmpty() || m_device.getText() == "Default"
			? "alsa" : m_device.getText().trim().toStdString(); break;
		}
#endif
		o.gainDb = static_cast<float>(m_gain.getValue());
		o.jackConnect = m_jackConnect.getToggleState();
		o.rawMidiCard = m_rawEnabled.getToggleState() ? m_rawCard.getText().trim().toStdString() : std::string();
		o.showDisclaimer = m_disclaimer.getToggleState();
#if defined(_WIN32) && defined(G1_BACKEND_JUCE) && !defined(G1_WINDOWS_MIDI)
		// Item 1 is "Automatic"; anything else is a device name, minus the "(not found)" marker
		// populate() appends when a remembered name is not among the system's devices right now.
		auto deviceName = [](const juce::ComboBox& _box) -> std::string
		{
			if(_box.getSelectedId() <= 1)
				return {};
			return _box.getText().upToFirstOccurrenceOf("  (not found)", false, false).toStdString();
		};
		o.pcPortOutDevice = deviceName(m_pcOutDevice);
		o.pcPortInDevice = deviceName(m_pcInDevice);
		o.midiOutDevice = deviceName(m_midiOutDeviceBox);
		o.midiInDevice = deviceName(m_midiInDeviceBox);
#endif
		o.save(g1app::EmuHost::defaultSettingsPath());
	}

	// What is actually in use right now, which is not always what the controls say: the driver
	// may have refused to open, or the environment may have overridden the file.
	void SettingsView::timerCallback()
	{
		const auto s = m_host.stats();
		juce::String text;
		text << "  audio:     " << juce::String(s.audio) << "\n";
		text << "  MIDI:      " << juce::String(s.midi) << "\n";
#ifndef G1_BACKEND_JUCE
		text << "  raw MIDI:  " << (s.rawMidi.empty() ? juce::String("none (no card taken over)") : juce::String(s.rawMidi));
#endif
		if(text != m_running.getText())
			m_running.setText(text, juce::dontSendNotification);
	}

	void SettingsView::paint(juce::Graphics& _g)
	{
		_g.fillAll(juce::Colour(0xff1c1c20));
		_g.setColour(juce::Colour(0xff35353c));
		for(const float y : {100.0f, 286.0f, 400.0f + g_midiExtra, 600.0f + g_midiExtra})
			_g.drawLine(static_cast<float>(g_margin), y, static_cast<float>(g_width - g_margin), y);
		_g.setColour(juce::Colours::white);
		_g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
		_g.drawText("ROM", g_margin, g_margin - 4, 200, 20, juce::Justification::centredLeft);
		_g.drawText("Audio", g_margin, 108, 200, 20, juce::Justification::centredLeft);
#ifdef G1_BACKEND_JUCE
		_g.drawText("MIDI ports", g_margin, 294, 200, 20, juce::Justification::centredLeft);
#else
		_g.drawText("Raw MIDI", g_margin, 294, 200, 20, juce::Justification::centredLeft);
#endif
		_g.drawText("Notice", g_margin, 408 + g_midiExtra, 200, 20, juce::Justification::centredLeft);
		_g.drawText("In use now", g_margin, 608 + g_midiExtra, 200, 20, juce::Justification::centredLeft);
	}

	void SettingsView::resized()
	{
		int y = g_margin + 22;
		auto row = [&](juce::Label& _label, juce::Component& _c, const int _w)
		{
			_label.setBounds(g_margin, y, g_labelW, g_rowH);
			_c.setBounds(g_margin + g_labelW + g_gap, y, _w, g_rowH);
			y += g_rowH + g_gap;
		};
		row(m_romLabel, m_romPath, g_width - g_margin * 2 - g_labelW - g_gap);
		m_romChoose.setBounds(g_margin + g_labelW + g_gap, y - 2, 100, 26);
		m_romFolder.setBounds(g_margin + g_labelW + g_gap + 110, y - 2, 170, 26);

		y = 108 + 22;
		row(m_audioLabel, m_audio, g_width - g_margin * 2 - g_labelW - g_gap);
		row(m_deviceLabel, m_device, g_width - g_margin * 2 - g_labelW - g_gap);
		row(m_gainLabel, m_gain, g_width - g_margin * 2 - g_labelW - g_gap);
		m_jackConnect.setBounds(g_margin + g_labelW + g_gap, y, g_width - g_margin - g_labelW - g_gap, g_rowH);

		y = 294 + 26;
		m_rawEnabled.setBounds(g_margin, y, g_width - g_margin * 2, g_rowH);
		y += g_rowH + g_gap;
		row(m_rawLabel, m_rawCard, 160);
		m_midiInfo.setBounds(g_margin, 320, g_width - g_margin * 2, g_midiInfoH);
#if defined(_WIN32) && defined(G1_BACKEND_JUCE) && !defined(G1_WINDOWS_MIDI)
		y = 320 + g_midiInfoH + 6;
		row(m_pcOutLabel, m_pcOutDevice, g_width - g_margin * 2 - g_labelW - g_gap);
		row(m_pcInLabel, m_pcInDevice, g_width - g_margin * 2 - g_labelW - g_gap);
		row(m_midiOutLabel, m_midiOutDeviceBox, g_width - g_margin * 2 - g_labelW - g_gap);
		row(m_midiInLabel, m_midiInDeviceBox, g_width - g_margin * 2 - g_labelW - g_gap);
#endif

		m_disclaimer.setBounds(g_margin, 432 + g_midiExtra, g_width - g_margin * 2, g_rowH);
		m_notice.setBounds(g_margin, 432 + g_midiExtra + g_rowH + 4, g_width - g_margin * 2, 124);

		m_running.setBounds(g_margin, 632 + g_midiExtra, g_width - g_margin * 2, 68);
		m_note.setBounds(g_margin, 704 + g_midiExtra, g_width - g_margin * 2 - 110, 52);
		m_close.setBounds(g_width - g_margin - 90, 728 + g_midiExtra, 90, 26);
	}
}
