#include "Settings.h"

#include "romfinder.h"

namespace g1gui
{
	namespace
	{
		constexpr int g_width = 560, g_height = 790;
		constexpr int g_labelW = 150, g_rowH = 28, g_gap = 10, g_margin = 18;

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
		label(m_deviceLabel, "ALSA device");
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

		// JACK here is PipeWire's JACK: on this system the emulator becomes a node of the graph
		// with out_1..out_4 and in_L/in_R, like the back panel.
		m_audio.addItem("JACK / PipeWire  (4 outputs, 2 inputs)", 1);
		m_audio.addItem("ALSA  (outputs 1/2 only)", 2);
		m_audio.addItem("None  (no sound)", 3);
		m_audio.setSelectedId(o.audio == "no" ? 3 : (o.audio == "jack" ? 1 : 2), juce::dontSendNotification);
		m_audio.onChange = [this] { updateEnabled(); apply(); };
		addAndMakeVisible(m_audio);

		m_device.setText(o.audio == "jack" || o.audio == "no" || o.audio == "alsa" ? "default" : o.audio, false);
		m_device.setTooltip("The ALSA device to open: default, hw:0,0, ...");
		m_device.onFocusLost = [this] { apply(); };
		m_device.onReturnKey = [this] { apply(); };
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

	void SettingsView::updateEnabled()
	{
		const bool alsa = m_audio.getSelectedId() == 2;
		const bool jack = m_audio.getSelectedId() == 1;
		m_device.setEnabled(alsa);
		m_deviceLabel.setEnabled(alsa);
		m_jackConnect.setEnabled(jack);
		m_gain.setEnabled(m_audio.getSelectedId() != 3);
		m_gainLabel.setEnabled(m_audio.getSelectedId() != 3);
		m_rawCard.setEnabled(m_rawEnabled.getToggleState());
		m_rawLabel.setEnabled(m_rawEnabled.getToggleState());
	}

	void SettingsView::apply()
	{
		auto& o = m_host.options();
		switch(m_audio.getSelectedId())
		{
		case 1:  o.audio = "jack"; break;
		case 3:  o.audio = "no"; break;
		default: o.audio = m_device.getText().trim().isEmpty() ? "alsa" : m_device.getText().trim().toStdString(); break;
		}
		o.gainDb = static_cast<float>(m_gain.getValue());
		o.jackConnect = m_jackConnect.getToggleState();
		o.rawMidiCard = m_rawEnabled.getToggleState() ? m_rawCard.getText().trim().toStdString() : std::string();
		o.showDisclaimer = m_disclaimer.getToggleState();
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
		text << "  raw MIDI:  " << (s.rawMidi.empty() ? juce::String("none (no card taken over)") : juce::String(s.rawMidi));
		if(text != m_running.getText())
			m_running.setText(text, juce::dontSendNotification);
	}

	void SettingsView::paint(juce::Graphics& _g)
	{
		_g.fillAll(juce::Colour(0xff1c1c20));
		_g.setColour(juce::Colour(0xff35353c));
		for(const float y : {100.0f, 286.0f, 400.0f, 600.0f})
			_g.drawLine(static_cast<float>(g_margin), y, static_cast<float>(g_width - g_margin), y);
		_g.setColour(juce::Colours::white);
		_g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
		_g.drawText("ROM", g_margin, g_margin - 4, 200, 20, juce::Justification::centredLeft);
		_g.drawText("Audio", g_margin, 108, 200, 20, juce::Justification::centredLeft);
		_g.drawText("Raw MIDI", g_margin, 294, 200, 20, juce::Justification::centredLeft);
		_g.drawText("Notice", g_margin, 408, 200, 20, juce::Justification::centredLeft);
		_g.drawText("In use now", g_margin, 608, 200, 20, juce::Justification::centredLeft);
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
		row(m_deviceLabel, m_device, 200);
		row(m_gainLabel, m_gain, g_width - g_margin * 2 - g_labelW - g_gap);
		m_jackConnect.setBounds(g_margin + g_labelW + g_gap, y, g_width - g_margin - g_labelW - g_gap, g_rowH);

		y = 294 + 26;
		m_rawEnabled.setBounds(g_margin, y, g_width - g_margin * 2, g_rowH);
		y += g_rowH + g_gap;
		row(m_rawLabel, m_rawCard, 160);

		m_disclaimer.setBounds(g_margin, 432, g_width - g_margin * 2, g_rowH);
		m_notice.setBounds(g_margin, 432 + g_rowH + 4, g_width - g_margin * 2, 124);

		m_running.setBounds(g_margin, 632, g_width - g_margin * 2, 52);
		m_note.setBounds(g_margin, 704, g_width - g_margin * 2 - 110, 52);
		m_close.setBounds(g_width - g_margin - 90, 728, 90, 26);
	}
}
