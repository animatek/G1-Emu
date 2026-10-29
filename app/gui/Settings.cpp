#include "Settings.h"
#include "osimage.h"

#include "Overlay.h"
#include "Panel.h"

#include "romfinder.h"
#ifdef G1_BACKEND_JUCE
#include "juceaudio.h"
#if defined(_WIN32) && !defined(G1_WINDOWS_MIDI)
#include "jucemidi.h"
#endif
#endif

namespace g1gui
{
	NativeTitleBarTheme::NativeTitleBarTheme(juce::Component& _window) : m_window(_window)
	{
		juce::Desktop::getInstance().addDarkModeSettingListener(this);
		apply();
	}

	NativeTitleBarTheme::~NativeTitleBarTheme()
	{
		juce::Desktop::getInstance().removeDarkModeSettingListener(this);
	}

	void NativeTitleBarTheme::apply()
	{
#if JUCE_WINDOWS
		// DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE = 20, &BOOL, sizeof(BOOL)),
		// looked up at run time so the build needs no import library for it.
		using SetAttribute = long(__stdcall*)(void*, unsigned long, const void*, unsigned long);
		static juce::DynamicLibrary dwm("dwmapi.dll");
		static const auto set = reinterpret_cast<SetAttribute>(dwm.getFunction("DwmSetWindowAttribute"));
		auto* peer = m_window.getPeer();
		if(!set || !peer)
			return;
		const int dark = juce::Desktop::getInstance().isDarkModeActive() ? 1 : 0;
		set(peer->getNativeHandle(), 20, &dark, sizeof(dark));
#endif
	}

	namespace
	{
		// Two columns: the ROM and the audio at the left, the MIDI ports at the right; below them,
		// what is in use and the notes, and the notice's row at the bottom, the card's Close at its
		// right. In the card's pixels.
		constexpr int g_labelW = 110, g_rowH = 28, g_gap = 10, g_headingH = 22;
		constexpr int g_colW = 430, g_colGap = 40, g_width = 2 * g_colW + g_colGap;
		constexpr int g_rightX = g_colW + g_colGap;
		constexpr int g_audioY = 184, g_belowY = 360, g_bottomY = 462, g_height = g_bottomY + g_rowH;
		// The manual MIDI pairing patch (Windows, until Windows MIDI Services can own ports;
		// jucemidi.h) adds four device rows to the MIDI column, under a longer note.
#if defined(_WIN32) && defined(G1_BACKEND_JUCE) && !defined(G1_WINDOWS_MIDI)
		constexpr int g_midiInfoH = 92;
#else
		constexpr int g_midiInfoH = 76;
#endif
	}

	juce::Font SettingsView::Look::getComboBoxFont(juce::ComboBox& _box)
	{
		const auto f = LookAndFeel_V4::getComboBoxFont(_box);
		return f.withHeight(f.getHeight() - Smaller);
	}

	juce::Label* SettingsView::Look::createSliderTextBox(juce::Slider& _slider)
	{
		auto* l = LookAndFeel_V4::createSliderTextBox(_slider);
		l->setFont(l->getFont().withHeight(l->getFont().getHeight() - Smaller));
		return l;
	}

	SettingsView::SettingsView(g1app::EmuHost& _host, std::function<void()> _restart) : m_host(_host), m_restart(std::move(_restart))
	{
		setLookAndFeel(&m_look);
		const auto& o = m_host.options();

		auto label = [this](juce::Label& _l, const juce::String& _text)
		{
			_l.setText(_text, juce::dontSendNotification);
			_l.setFont(juce::FontOptions(Look::TitleFontHeight));
			_l.setColour(juce::Label::textColourId, juce::Colours::white);
			_l.setJustificationType(juce::Justification::centredRight);
			addAndMakeVisible(_l);
		};
		label(m_romLabel, "ROM in use");
		label(m_audioLabel, "Audio driver");
		label(m_deviceLabel, "Audio device");
		label(m_gainLabel, "Output level");
		label(m_rawLabel, "Card ID");
		label(m_modelLabel, "Model");

		// The ROM is the user's and always will be: this says which one is running and lets them
		// point at another. A change needs a restart, like any other ROM swap would on hardware.
		m_romPath.setColour(juce::Label::textColourId, juce::Colour(0xffc8ccd0));
		m_romPath.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
		const auto& romInUse = o.romInUse.empty() ? o.rom : o.romInUse;
		m_romPath.setText(romInUse.empty() ? "none" : romInUse, juce::dontSendNotification);
		m_romPath.setTooltip(romInUse);
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

		// Regular or Expanded: the G1 with the expansion board has 8 DSPs instead of 4, and the same
		// OS gives each patch about twice the voices. A new G1 is needed: changing it offers to restart
		// now, or it takes effect on the next start or Restart.
		m_model.addItem("Regular  (4 DSPs)", 1);
		m_model.addItem("Expanded  (8 DSPs, twice the voices)", 2);
		m_model.addItem("Micro Modular  (1 DSP, its own OS)", 3);
		m_model.setSelectedId(o.micro ? 3 : o.expanded ? 2 : 1, juce::dontSendNotification);
		m_model.setTooltip("The G1 with or without the expansion board, or the Micro Modular (one DSP, light on the CPU); on the next start or Restart");
		m_model.onChange = [this]
		{
			apply();
			// A model other than the running one: offer the restart now (the Micro's box has no power
			// button, and from the G1's panel it saves a step).
			const auto wanted = m_model.getSelectedId() == 3 ? g1::g_microDsps : m_model.getSelectedId() == 2 ? g1::g_expandedDsps : g1::g_regularDsps;
			if(wanted != m_host.mc().dspCount())
				askThenRestart("Model", "The G1 restarts as the " + juce::String(g1app::HostOptions::modelName(wanted)) + ".\n\nYour banks are "
					"not touched; each model keeps its own flash where it has one.", "Restart now", [] {});
		};
		addAndMakeVisible(m_model);

		// The Micro runs its own OS, which G1-Emu does not ship: Clavia's updater for it (the .exe) is
		// read as it is.
		label(m_microOsLabel, "Micro OS");
		m_microOsPath.setColour(juce::Label::textColourId, juce::Colour(0xffc8ccd0));
		m_microOsPath.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
		m_microOsPath.setMinimumHorizontalScale(0.7f);
		addAndMakeVisible(m_microOsPath);
		m_microOsChoose.setTooltip("Clavia's Micro Modular OS updater (the .exe), or an OS image");
		m_microOsChoose.onClick = [this] { chooseMicroOs(); };
		addAndMakeVisible(m_microOsChoose);
		showMicroOs();

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

		// Off, the box offers the default card (the USB MIDI gadget's ID), so ticking it again works
		m_rawCard.setText(o.rawMidiCard.empty() ? g1app::HostOptions{}.rawMidiCard : o.rawMidiCard, false);
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
				_l.setFont(juce::FontOptions(Look::TitleFontHeight));
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
		addChildComponent(m_notice);
		m_noticeToggle.onClick = [this] { setNoticeOpen(!m_noticeOpen); };
		addAndMakeVisible(m_noticeToggle);

		m_banks.setTooltip("Empty the banks, or bring back a copy saved before. The banks in use are kept in a file, not lost");
		m_banks.onClick = [this]
		{
			juce::PopupMenu menu;
			menu.addItem("Restore banks from a saved copy...", [this] { chooseBanks(); });
			menu.addItem("Empty banks...", [this]
			{
				askThenRestart("Empty banks", "The G1 restarts with empty banks.\n\nThe G1's factory patches are not in the ROM (they came in "
					"each synth's flash), so this does not bring them back. Your banks are not lost: they are kept as "
					"flash-before-reset-<date>.bin, and Restore banks brings them back. Plugin instances already in a project keep their own.",
					"Empty the banks", [this] { m_host.resetFlashOnRestart(); });
			});
			menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_banks));
		};
		addAndMakeVisible(m_banks);
		m_os.setTooltip("Install an OS through the PC Port, as on a real G1, or run the ROM's own again");
		m_os.onClick = [this]
		{
			juce::PopupMenu menu;
			menu.addItem("Update mode (install an OS)...", !m_host.updateMode(), false, [this]
			{
				askThenRestart("Update mode", "The G1 restarts waiting for an OS, as a real G1 does with Shift + Store held at "
					"power-on: the display says Update utility.\n\nSend the OS to the PC Port with Clavia's updater (tick Safe,"
					" Slower). When the display asks Update flash?, press Store; at Update completed, "
					"restart the G1 with the power button (bottom right, beside About) and it boots the new OS, kept in its "
					"folder.\n\nYour banks are not touched. To leave without updating, use the power button as well.", "Restart in update mode",
					[this] { m_host.updateModeOnRestart(); });
			});
			const auto& os = m_host.options().os;
			menu.addItem(os.empty() ? juce::String("The ROM's own OS (in use)") : juce::String("The ROM's own OS..."), !os.empty(), os.empty(), [this]
			{
				askThenRestart("The ROM's own OS", "The G1 restarts with the OS in the ROM instead of the updated one.\n\nThe updated OS "
					"stays in its folder; a new update brings it back.", "Use the ROM's OS",
					[this] { m_host.options().os.clear(); m_host.options().save(g1app::EmuHost::defaultSettingsPath()); });
			});
			menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&m_os));
		};
		addAndMakeVisible(m_os);

		for(auto* l : {&m_running, &m_note})
		{
			l->setColour(juce::Label::textColourId, juce::Colour(0xff9aa0a6));
			l->setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::plain));
			l->setJustificationType(juce::Justification::topLeft);
			addAndMakeVisible(*l);
		}
		m_note.setText("The level applies now; the rest, on the next start.\n"
			"Environment variables win over these settings.\n"
			"Saved in " + juce::String(g1app::EmuHost::defaultSettingsPath()), juce::dontSendNotification);
		m_note.setTooltip(g1app::EmuHost::defaultSettingsPath());

		updateEnabled();
		timerCallback();
		startTimerHz(4);
		setSize(g_width, g_height);
		setNoticeOpen(false);
	}

	// The notice about Clavia, ROMs and support is there to be read once, not every time the
	// settings open: its text takes the settings' place while it is read.
	void SettingsView::setNoticeOpen(const bool _open)
	{
		m_noticeOpen = _open;
		m_notice.setVisible(_open);
		if(_open)
			m_notice.toFront(false);
		m_noticeToggle.setButtonText(_open ? "Back to the settings" : "Read the notice");
		repaint();
	}

	// Picking a ROM writes it down, and offers to restart the G1 with it at once: the emulator is
	// running on the old one, and swapping it under a booted OS is not something the hardware does
	// either, but a G1 switched off and on is. Cancel keeps it for the next start.
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
				const auto hint = g1app::osImageHint(file.getFullPathName().toStdString());
				juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Not the ROM G1-Emu needs",
					file.getFullPathName() + "\n\n" + (hint.empty() ? juce::String(check.what()) : juce::String(hint)));
				return;
			}
			m_host.options().rom = file.getFullPathName().toStdString();
			m_romPath.setText(file.getFullPathName(), juce::dontSendNotification);
			m_romPath.setTooltip(file.getFullPathName());
			apply();
			if(file.getFullPathName().toStdString() == m_host.options().romInUse)
				return;
			askThenRestart("The ROM", "The G1 restarts with\n" + file.getFileName() + "\n\nYour banks are not touched. "
				"The OS in the ROM is the one that runs, unless the settings name another OS (Settings > OS...). "
				"Cancel keeps the choice for the next start.", "Restart with this ROM", [] {});
		});
	}

	void SettingsView::showMicroOs()
	{
		g1app::OsImage image;
		std::string note;
		const bool found = g1app::findMicroOs(m_host.options().microOs, image, note);
		const auto last = note.rfind('\n');
		const auto line = last == std::string::npos ? note : note.substr(last + 1);
		m_microOsPath.setText(found ? juce::File(juce::String(line).fromFirstOccurrenceOf("Micro OS: ", false, false)).getFileName() : "not found (see the tooltip)", juce::dontSendNotification);
		m_microOsPath.setTooltip(juce::String(line));
	}

	void SettingsView::chooseMicroOs()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Choose Clavia's Micro Modular OS (the updater .exe)",
			juce::File(m_host.options().microOs).existsAsFile() ? juce::File(m_host.options().microOs).getParentDirectory()
				: juce::File(g1app::publicRomFolder()), "*.exe;*.EXE;*.bin;*.BIN");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this](const juce::FileChooser& _c)
		{
			const auto file = _c.getResult();
			if(file == juce::File())
				return;
			g1app::OsImage image;
			std::string why;
			if(!g1app::readOsFile(file.getFullPathName().toStdString(), image, why) || (image.fromUpdater && !image.isMicro()))
			{
				juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Not the Micro Modular's OS",
					file.getFullPathName() + "\n\n" + (why.empty() ? juce::String("That updater is for another model: \"") + image.model + "\"." : juce::String(why)));
				return;
			}
			m_host.options().microOs = file.getFullPathName().toStdString();
			showMicroOs();
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
		o.expanded = m_model.getSelectedId() == 2;
		o.micro = m_model.getSelectedId() == 3;
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
		text << "  model:     " << g1app::HostOptions::modelName(s.dspCount) << " (" << static_cast<int>(s.dspCount) << " DSPs)\n";
		text << "  audio:     " << juce::String(s.audio) << "\n";
		text << "  MIDI:      " << juce::String(s.midi) << "\n";
#ifndef G1_BACKEND_JUCE
		text << "  raw MIDI:  " << (s.rawMidi.empty() ? juce::String("none (no card taken over)") : juce::String(s.rawMidi));
#endif
		if(text != m_running.getText())
			m_running.setText(text, juce::dontSendNotification);
	}

	SettingsView::~SettingsView()
	{
		setLookAndFeel(nullptr);
	}

	// The card paints the background; this, the headings and the rules between the parts. While the
	// notice is read, it covers them.
	void SettingsView::paint(juce::Graphics& _g)
	{
		if(m_noticeOpen)
			return;
		_g.setColour(overlay::Rule);
		_g.drawLine(0.0f, g_audioY - 8.0f, static_cast<float>(g_colW), g_audioY - 8.0f);
		_g.drawLine(0.0f, static_cast<float>(g_belowY), static_cast<float>(g_width), static_cast<float>(g_belowY));
		_g.setColour(juce::Colours::white);
		_g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
		auto heading = [&](const char* _text, const int _x, const int _y) { _g.drawText(_text, _x, _y, g_colW, g_headingH - 2, juce::Justification::centredLeft); };
		heading("ROM and model", 0, 0);
		heading("Audio", 0, g_audioY);
#ifdef G1_BACKEND_JUCE
		heading("MIDI ports", g_rightX, 0);
#else
		heading("Raw MIDI", g_rightX, 0);
#endif
		heading("In use now", 0, g_belowY + 8);
	}

	void SettingsView::resized()
	{
		// A row: its label at _x, the control after it, as wide as the column allows.
		auto row = [](juce::Label& _label, juce::Component& _c, const int _x, int& _y)
		{
			_label.setBounds(_x, _y, g_labelW, g_rowH);
			_c.setBounds(_x + g_labelW + g_gap, _y, g_colW - g_labelW - g_gap, g_rowH);
			_y += g_rowH + g_gap;
		};

		int y = g_headingH;
		row(m_romLabel, m_romPath, 0, y);
		m_romChoose.setBounds(g_labelW + g_gap, y - 2, 100, 26);
		m_romFolder.setBounds(g_labelW + g_gap + 110, y - 2, 170, 26);
		y += g_rowH + g_gap;
		row(m_modelLabel, m_model, 0, y);
		m_microOsLabel.setBounds(0, y, g_labelW, g_rowH);
		m_microOsPath.setBounds(g_labelW + g_gap, y, g_colW - g_labelW - 2 * g_gap - 100, g_rowH);
		m_microOsChoose.setBounds(g_colW - 100, y, 100, 26);

		y = g_audioY + g_headingH;
		row(m_audioLabel, m_audio, 0, y);
		row(m_deviceLabel, m_device, 0, y);
		row(m_gainLabel, m_gain, 0, y);
		m_jackConnect.setBounds(g_labelW + g_gap, y, g_colW - g_labelW - g_gap, g_rowH);

		y = g_headingH;
		m_rawEnabled.setBounds(g_rightX, y, g_colW, g_rowH);
		y += g_rowH + g_gap;
		row(m_rawLabel, m_rawCard, g_rightX, y);
		m_midiInfo.setBounds(g_rightX, g_headingH, g_colW, g_midiInfoH);
#if defined(_WIN32) && defined(G1_BACKEND_JUCE) && !defined(G1_WINDOWS_MIDI)
		y = g_headingH + g_midiInfoH + 6;
		row(m_pcOutLabel, m_pcOutDevice, g_rightX, y);
		row(m_pcInLabel, m_pcInDevice, g_rightX, y);
		row(m_midiOutLabel, m_midiOutDeviceBox, g_rightX, y);
		row(m_midiInLabel, m_midiInDeviceBox, g_rightX, y);
#endif

		m_running.setBounds(0, g_belowY + 8 + g_headingH, g_colW, 66);
		m_note.setBounds(g_rightX, g_belowY + 8 + g_headingH, g_colW, 52);

		// The notice's text takes everything above its row.
		m_notice.setBounds(0, 0, g_width, g_bottomY - g_gap);
		m_disclaimer.setBounds(0, g_bottomY, 230, g_rowH);
		m_noticeToggle.setBounds(240, g_bottomY + 1, 170, 26);
		// Between the notice's and the card's Close.
		m_banks.setBounds(430, g_bottomY + 1, 170, 26);
		m_os.setBounds(610, g_bottomY + 1, 170, 26);
	}

	// A G1-Emu flash to bring back, from the folder the saved ones are in.
	void SettingsView::chooseBanks()
	{
		const auto dir = juce::File(g1app::EmuHost::defaultFlashPath()).getParentDirectory();
		m_chooser = std::make_unique<juce::FileChooser>("Banks to restore (a flash saved before)", dir, "*.bin");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this](const juce::FileChooser& _c)
		{
			const auto file = _c.getResult();
			if(file == juce::File())
				return;
			if(file.getSize() != 1024 * 1024)
			{
				juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Not a G1-Emu flash",
					file.getFullPathName() + "\n\nA G1-Emu flash is 1 MB, as flash.bin and the copies beside it.");
				return;
			}
			askThenRestart("Restore banks", "The G1 restarts with the banks in\n" + file.getFileName() + "\n\nThe banks in use now are kept "
				"as flash-before-reset-<date>.bin.", "Restore", [this, path = file.getFullPathName().toStdString()] { m_host.resetFlashOnRestart(path); });
		});
	}

	void SettingsView::askThenRestart(const juce::String& _title, const juce::String& _message, const juce::String& _yes, std::function<void()> _then)
	{
		juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::QuestionIcon)
			.withTitle(_title).withMessage(_message).withButton(_yes).withButton("Cancel"),
			[v = juce::Component::SafePointer<SettingsView>(this), then = std::move(_then)](const int _result)
			{
				if(_result != 1 || !v)
					return;
				then();
				if(v->m_restart)
					v->m_restart();
			});
	}
}
