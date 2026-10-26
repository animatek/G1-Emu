#include "hostconfig.h"
#include "osimage.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace g1app
{
	const char* const HostOptions::audioNames[3] = {"jack", "alsa", "no"};

	std::string defaultFlashPath()
	{
#ifdef _WIN32
		// HOME is normally unset in a native Windows process. Falling back to "." made the
		// settings and flash follow the executable's working directory, so launching the same
		// build from Explorer and a terminal produced two independent G1s. APPDATA is the native,
		// per-user location and remains stable regardless of how the program was started.
		if(const char* appData = std::getenv("APPDATA"))
			return (std::filesystem::path(appData) / "Animatek/G1-Emu/flash.bin").string();
#endif
		const char* home = std::getenv("HOME");
		return std::string(home ? home : ".") + "/.local/share/Animatek/G1-Emu/flash.bin";
	}

	std::string defaultSettingsPath()
	{
		return (std::filesystem::path(defaultFlashPath()).parent_path() / "settings.conf").string();
	}

	bool HostOptions::load(const std::string& _path)
	{
		std::ifstream f(_path);
		if(!f)
			return false;
		std::string line;
		while(std::getline(f, line))
		{
			const auto eq = line.find('=');
			if(line.empty() || line[0] == '#' || eq == std::string::npos)
				continue;
			auto trim = [](std::string _s)
			{
				const auto a = _s.find_first_not_of(" \t\r");
				const auto b = _s.find_last_not_of(" \t\r");
				return a == std::string::npos ? std::string() : _s.substr(a, b - a + 1);
			};
			const auto key = trim(line.substr(0, eq));
			const auto value = trim(line.substr(eq + 1));
			if(key == "audio")				audio = value;
			else if(key == "gainDb")		gainDb = static_cast<float>(std::atof(value.c_str()));
			else if(key == "jackConnect")	jackConnect = value != "0";
			else if(key == "directLink")	directLink = value != "0";
			else if(key == "rawMidiCard")	rawMidiCard = value;
			else if(key == "rom")			rom = value;
			else if(key == "os")			os = value;
			else if(key == "model")			{ expanded = value == "expanded"; micro = value == "micro"; }
			else if(key == "microOs")		microOs = value;
			else if(key == "showDisclaimer") showDisclaimer = value != "0";
			else if(key == "extrasOpen")	extrasOpen = value != "0";
			else if(key == "knobDisplays")	knobDisplays = value != "0";
			else if(key == "knobFollowsPatch") knobFollowsPatch = value != "0";
			else if(key == "midiCcSelectedSlots") selectedControllers = value != "0";
			else if(key == "octaveShift")	octaveShift = value != "0";
			else if(key == "randomExclude")	randomExclude = knobListFromString(value);
			else if(key == "tooltips")	tooltips = value != "0";
			else if(key == "presetsHideEmpty") presetsHideEmpty = value != "0";
			else if(key == "masterVolume")	masterVolume = std::atoi(value.c_str());
			else if(key == "panelScale")	panelScale = static_cast<float>(std::atof(value.c_str()));
			else if(key == "pcPortOutDevice") pcPortOutDevice = value;
			else if(key == "pcPortInDevice")  pcPortInDevice = value;
			else if(key == "midiOutDevice")   midiOutDevice = value;
			else if(key == "midiInDevice")    midiInDevice = value;
		}
		return true;
	}

	std::string knobListToString(const uint32_t _knobs)
	{
		std::string text;
		for(uint32_t k = 0; k < 18; ++k)
			if(_knobs & (1u << k))
				text += (text.empty() ? "" : " ") + std::to_string(k + 1);
		return text;
	}

	uint32_t knobListFromString(const std::string& _text)
	{
		uint32_t knobs = 0;
		std::istringstream in(_text);
		std::string word;
		while(in >> word)
		{
			const int n = std::atoi(word.c_str());
			if(n >= 1 && n <= 18)
				knobs |= 1u << (n - 1);
		}
		return knobs;
	}

	std::vector<uint8_t> HostOptions::loadOs(std::string& _note) const
	{
		std::string path = os;
		if(const char* v = std::getenv("G1_OS"))
			path = v;
		_note.clear();
		OsImage image;
		if(path.empty() && dspCount() == g1::g_microDsps)
		{
			// Its own OS: the rack's only knows four or eight DSPs.
			if(!findMicroOs(microOs, image, _note))
				return {};
			return image.bytes;
		}
		if(path.empty())
			return {};
		std::string why;
		if(!readOsFile(path, image, why))
		{
			_note = "OS image " + path + " not used: " + why;
			return {};
		}
		_note = "OS: " + path;
		return image.bytes;
	}

	uint32_t HostOptions::dspCount() const
	{
		bool e = expanded, m = micro;
		if(const char* v = std::getenv("G1_MODEL"))
		{
			e = std::string(v) == "expanded";
			m = std::string(v) == "micro";
		}
		return m ? g1::g_microDsps : e ? g1::g_expandedDsps : g1::g_regularDsps;
	}

	bool HostOptions::save(const std::string& _path) const
	{
		// What the environment sets for one run (the scripts, the test bench, G1_SNAPSHOTS) is not
		// written over the file's own: those keys keep what the file says, as G1_ROM's ROM does.
		HostOptions o = *this;
		if(HostOptions file; file.load(_path))
		{
			const auto keep = [](const char* _var, auto& _mine, const auto& _file) { if(std::getenv(_var)) _mine = _file; };
			keep("G1_ROM", o.rom, file.rom);
			keep("G1_AUDIO", o.audio, file.audio);
			keep("G1_GAIN_DB", o.gainDb, file.gainDb);
			keep("G1_JACK_CONNECT", o.jackConnect, file.jackConnect);
			keep("G1_DIRECT_LINK", o.directLink, file.directLink);
			keep("G1_RAWMIDI", o.rawMidiCard, file.rawMidiCard);
			keep("G1_PCPORT_OUT", o.pcPortOutDevice, file.pcPortOutDevice);
			keep("G1_PCPORT_IN", o.pcPortInDevice, file.pcPortInDevice);
			keep("G1_MIDI_OUT", o.midiOutDevice, file.midiOutDevice);
			keep("G1_MIDI_IN", o.midiInDevice, file.midiInDevice);
		}
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(_path).parent_path(), ec);
		std::ofstream f(_path, std::ios::trunc);
		if(!f)
			return false;
		f << "# G1-Emu settings. The G1_* environment variables still win over this file.\n"
		  << "audio = " << o.audio << "\n"
		  << "gainDb = " << o.gainDb << "\n"
		  << "jackConnect = " << (o.jackConnect ? 1 : 0) << "\n"
		  << "directLink = " << (o.directLink ? 1 : 0) << "\n"
		  << "rawMidiCard = " << o.rawMidiCard << "\n"
		  << "rom = " << o.rom << "\n"
		  << "os = " << o.os << "\n"
		  << "model = " << (o.micro ? "micro" : o.expanded ? "expanded" : "regular") << "\n"
		  << "microOs = " << o.microOs << "\n"
		  << "showDisclaimer = " << (o.showDisclaimer ? 1 : 0) << "\n"
		  << "extrasOpen = " << (o.extrasOpen ? 1 : 0) << "\n"
		  << "knobDisplays = " << (o.knobDisplays ? 1 : 0) << "\n"
		  << "knobFollowsPatch = " << (o.knobFollowsPatch ? 1 : 0) << "\n"
		  << "midiCcSelectedSlots = " << (o.selectedControllers ? 1 : 0) << "\n"
		  << "octaveShift = " << (o.octaveShift ? 1 : 0) << "\n"
		  << "randomExclude = " << knobListToString(o.randomExclude) << "\n"
		  << "tooltips = " << (o.tooltips ? 1 : 0) << "\n"
		  << "presetsHideEmpty = " << (o.presetsHideEmpty ? 1 : 0) << "\n"
		  << "masterVolume = " << o.masterVolume << "\n"
		  << "panelScale = " << o.panelScale << "\n"
		  << "pcPortOutDevice = " << o.pcPortOutDevice << "\n"
		  << "pcPortInDevice = " << o.pcPortInDevice << "\n"
		  << "midiOutDevice = " << o.midiOutDevice << "\n"
		  << "midiInDevice = " << o.midiInDevice << "\n";
		return f.good();
	}
}
