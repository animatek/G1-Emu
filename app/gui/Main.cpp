// g1gui: the emulated G1 with its panel in a window.
//
//   g1gui [ROM] [FLASH]
//
// Without a ROM argument it looks in Roms/ of the current directory and of the G1-Emu tree. The
// flash works like g1run (default ~/.local/share/Animatek/G1-Emu/flash.bin). Everything else
// (MIDI, JACK audio, G1_* variables) is the same as in g1run: EmuHost does it.

#include "Panel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace g1gui
{
	namespace
	{
		constexpr const char* g_romName = "NORD-MODULAR-RACK-VER-3.03.BIN";

		// Shown at startup until the user ticks "Don't show this again". The same text is in the
		// README ("Please read this first").
		const char* const g_disclaimer =
			"G1-Emu is an independent, open-source emulator of the Nord Modular G1.\n\n"
			"- It is not affiliated with, endorsed by or connected to Clavia DMI in any way. "
			"\"Nord\" and \"Nord Modular\" are trademarks of Clavia DMI.\n\n"
			"- No ROMs or firmware are included, and none will ever be provided. Please do not ask "
			"for them: you will not find them here.\n\n"
			"- There is no support. This is a pre-alpha community project, made in spare time. Bug "
			"reports and contributions are welcome on GitHub; requests for help, ROMs or builds are not.";

		juce::File findRom(const juce::StringArray& _args)
		{
			if(!_args.isEmpty() && juce::File::isAbsolutePath(_args[0]))
				return juce::File(_args[0]);
			if(!_args.isEmpty())
				return juce::File::getCurrentWorkingDirectory().getChildFile(_args[0]);
			for(const auto& dir : {juce::File::getCurrentWorkingDirectory(), juce::File(G1_SOURCE_DIR)})
				if(dir.getChildFile("Roms").getChildFile(g_romName).existsAsFile())
					return dir.getChildFile("Roms").getChildFile(g_romName);
			return {};
		}
	}

	class MainWindow : public juce::DocumentWindow
	{
	public:
		MainWindow(g1app::EmuHost& _host) : DocumentWindow("G1-Emu", juce::Colours::black, DocumentWindow::closeButton | DocumentWindow::minimiseButton)
		{
			setUsingNativeTitleBar(true);
			setContentOwned(new Panel(_host), true);
			setResizable(false, false);
			centreWithSize(getWidth(), getHeight());
			setVisible(true);
		}
		void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
	};

	class App : public juce::JUCEApplication
	{
	public:
		const juce::String getApplicationName() override { return "G1-Emu"; }
		const juce::String getApplicationVersion() override { return "0.1"; }
		bool moreThanOneInstanceAllowed() override { return false; }

		void initialise(const juce::String& _cmd) override
		{
			const auto args = juce::StringArray::fromTokens(_cmd, true);
			const auto rom = findRom(args);
			std::string log;
			if(!rom.existsAsFile() || !m_host.start(rom.getFullPathName().toStdString(), args.size() > 1 ? args[1].toStdString() : "", log))
			{
				juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "G1-Emu",
					"Cannot start the emulated G1.\n\n" + juce::String(log) + (rom.existsAsFile() ? "" : "\nThe ROM (" + juce::String(g_romName)
					+ ") is missing in Roms/. G1-Emu does not include any ROM and none will be provided: you need a dump of your own."));
				return;
			}
			std::printf("%s", log.c_str());
			m_window = std::make_unique<MainWindow>(m_host);
			showDisclaimer();
		}

		void shutdown() override
		{
			m_window.reset();
			m_host.stop();	// saves the flash
		}

	private:
		// The notice about Clavia, ROMs and support, until the user asks not to see it again. The
		// choice is kept in the user settings (~/.config/G1-Emu.settings on Linux).
		void showDisclaimer()
		{
			juce::PropertiesFile::Options opts;
			opts.applicationName = "G1-Emu";
			opts.filenameSuffix = ".settings";
			opts.osxLibrarySubFolder = "Application Support";
			m_settings.setStorageParameters(opts);
			if(m_settings.getUserSettings()->getBoolValue("hideDisclaimer", false))
				return;

			auto* w = new juce::AlertWindow("Please read this first", g_disclaimer, juce::MessageBoxIconType::InfoIcon, m_window.get());
			m_dontShow = std::make_unique<juce::ToggleButton>("Don't show this again");
			m_dontShow->setSize(260, 24);
			m_dontShow->setName({});	// AlertWindow draws a custom component's name as a label
			w->addCustomComponent(m_dontShow.get());
			w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
			w->enterModalState(true, juce::ModalCallbackFunction::create([this](int)
			{
				if(m_dontShow && m_dontShow->getToggleState())
				{
					m_settings.getUserSettings()->setValue("hideDisclaimer", true);
					m_settings.saveIfNeeded();
				}
			}), true);
		}

		g1app::EmuHost m_host;
		std::unique_ptr<MainWindow> m_window;
		juce::ApplicationProperties m_settings;
		std::unique_ptr<juce::ToggleButton> m_dontShow;
	};
}

START_JUCE_APPLICATION(g1gui::App)
