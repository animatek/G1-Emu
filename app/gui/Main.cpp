// g1gui: el G1 emulado con su panel en una ventana.
//
//   g1gui [ROM] [FLASH]
//
// Sin ROM, la busca en Roms/ del directorio actual y del arbol de G1-Emu. La flash, como g1run
// (por defecto ~/.local/share/Animatek/G1-Emu/flash.bin). Todo lo demas (MIDI, audio por JACK,
// variables G1_*) es igual que en g1run: lo hace EmuHost.

#include "Panel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace g1gui
{
	namespace
	{
		constexpr const char* g_romName = "NORD-MODULAR-RACK-VER-3.03.BIN";

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
					"No puedo arrancar el G1 emulado.\n\n" + juce::String(log) + (rom.existsAsFile() ? "" : "\nFalta la ROM (" + juce::String(g_romName) + ") en Roms/."));
				return;
			}
			std::printf("%s", log.c_str());
			m_window = std::make_unique<MainWindow>(m_host);
		}

		void shutdown() override
		{
			m_window.reset();
			m_host.stop();	// guarda la flash
		}

	private:
		g1app::EmuHost m_host;
		std::unique_ptr<MainWindow> m_window;
	};
}

START_JUCE_APPLICATION(g1gui::App)
