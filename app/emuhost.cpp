#include "emuhost.h"

#include "romfinder.h"

#ifdef G1_BACKEND_JUCE
#include "juceaudio.h"
#include "jucemidi.h"
#else
#include "alsaaudio.h"
#include "alsamidi.h"
#ifdef G1_HAVE_JACK
#include "jackaudio.h"
#else
namespace g1app { class JackAudio {}; }
#endif
#endif

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <sstream>
#ifdef __linux__
#include <unistd.h>
#endif

namespace g1app
{
	namespace
	{
		bool loadFile(const std::string& _path, std::vector<uint8_t>& _data)
		{
			std::ifstream f(_path, std::ios::binary);
			if(!f)
				return false;
			_data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			return true;
		}

		// CPU seconds (user + system) of the whole process. Only Linux has /proc; elsewhere the
		// load figure in the status line is simply not shown.
		double processCpuSeconds()
		{
#ifndef __linux__
			return 0;
#else
			std::ifstream f("/proc/self/stat");
			std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
			const auto close = s.rfind(')');
			if(close == std::string::npos)
				return 0;
			std::istringstream in(s.substr(close + 2));
			std::string field;
			unsigned long long utime = 0, stime = 0;
			for(int i = 3; i <= 15 && in >> field; ++i)
			{
				if(i == 14) utime = std::stoull(field);
				if(i == 15) stime = std::stoull(field);
			}
			return static_cast<double>(utime + stime) / static_cast<double>(sysconf(_SC_CLK_TCK));
#endif
		}

		// The snd-virmidi card reserved for the G1 (its ID is G1Emu unless G1_RAWMIDI names
		// another one). Returns its card number, or -1 if it is not loaded.
		int rawMidiCard(const std::string& _id)
		{
#ifndef __linux__
			(void)_id;		// only Linux hides application ports from raw MIDI programs
			return -1;
#else
			if(_id.empty())
				return -1;
			const std::string& id = _id;
			std::error_code ec;
			for(const auto& entry : std::filesystem::directory_iterator("/proc/asound", ec))
			{
				const auto name = entry.path().filename().string();
				if(name.rfind("card", 0) != 0)
					continue;
				std::ifstream f(entry.path() / "id");
				std::string line;
				if(!std::getline(f, line) || line != id)
					continue;
				try { return std::stoi(name.substr(4)); } catch(...) { return -1; }
			}
			return -1;
#endif
		}

		// The outputs carry DSP 3's X:$5F offset ($155, almost nothing); on the hardware the
		// output capacitor removes it. It is subtracted so no DC reaches the sound card.
		constexpr int32_t g_dc = 0x155;
	}

	const char* const EmuHost::Options::audioNames[3] = {"jack", "alsa", "no"};

	EmuHost::EmuHost() = default;

	EmuHost::~EmuHost()
	{
		stop();
	}

	std::string EmuHost::defaultFlashPath()
	{
		const char* home = std::getenv("HOME");
		return std::string(home ? home : ".") + "/.local/share/Animatek/G1-Emu/flash.bin";
	}

	std::string EmuHost::defaultSettingsPath()
	{
		return (std::filesystem::path(defaultFlashPath()).parent_path() / "settings.conf").string();
	}

	bool EmuHost::Options::load(const std::string& _path)
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
			else if(key == "rawMidiCard")	rawMidiCard = value;
			else if(key == "rom")			rom = value;
			else if(key == "showDisclaimer") showDisclaimer = value != "0";
		}
		return true;
	}

	bool EmuHost::Options::save(const std::string& _path) const
	{
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(_path).parent_path(), ec);
		std::ofstream f(_path, std::ios::trunc);
		if(!f)
			return false;
		f << "# G1-Emu settings. The G1_* environment variables still win over this file.\n"
		  << "audio = " << audio << "\n"
		  << "gainDb = " << gainDb << "\n"
		  << "jackConnect = " << (jackConnect ? 1 : 0) << "\n"
		  << "rawMidiCard = " << rawMidiCard << "\n"
		  << "rom = " << rom << "\n"
		  << "showDisclaimer = " << (showDisclaimer ? 1 : 0) << "\n";
		return f.good();
	}

	bool EmuHost::start(const std::string& _romPath, const std::string& _flashPath, std::string& _log)
	{
		m_options.load(defaultSettingsPath());

		// The environment wins over whatever the window asked for: the scripts and the test bench
		// are driven that way and must keep working with the settings file in place.
		if(const char* v = std::getenv("G1_ROM"))
			m_options.rom = v;
		if(const char* v = std::getenv("G1_AUDIO"))
			m_options.audio = v;
		if(const char* v = std::getenv("G1_GAIN_DB"))
			m_options.gainDb = static_cast<float>(std::atof(v));
		if(const char* v = std::getenv("G1_JACK_CONNECT"))
			m_options.jackConnect = std::string(v) != "0";
		if(const char* v = std::getenv("G1_RAWMIDI"))
			m_options.rawMidiCard = std::string(v) == "0" ? "" : v;

		// The ROM. G1-Emu ships none, so not finding one is the normal first run, not a crash:
		// the message has to say what to put where, and what was wrong with what is already there.
		m_romProblem.clear();
		const auto search = findRom(_romPath, m_options.rom);
		std::vector<uint8_t> rom;
		if(!search.found() || !inspectRom(search.path, rom).ok())
		{
			std::string msg = "G1-Emu needs the 512 KB ROM of a Nord Modular rack, and it includes none.\n\n";
			if(!search.rejected.empty())
			{
				msg += "What was looked at and why it does not serve:\n";
				for(const auto& [file, why] : search.rejected)
					msg += "  " + file + "\n      " + why + "\n";
				msg += "\n";
			}
			msg += "Put a dump of your own unit's ROM in:\n  " + publicRomFolder() + "\n";
			if(search.looked.size() > 1)
			{
				msg += "\nAlso looked in:\n";
				for(size_t i = 1; i < search.looked.size(); ++i)
					msg += "  " + search.looked[i] + "\n";
			}
			m_romProblem = msg;
			_log += msg;
			return false;
		}
		m_options.rom = search.path;
		_log += "ROM: " + search.path + "\n";

		m_flashPath = _flashPath.empty() ? defaultFlashPath() : _flashPath;
		m_mc = std::make_unique<g1::Microcontroller>(rom);

		std::vector<uint8_t> flash;
		if(loadFile(m_flashPath, flash) && flash.size() == g1::Flash::Size)
		{
			m_mc->getFlash().data() = flash;
			_log += "flash loaded from " + m_flashPath + "\n";
		}
		else
		{
			m_mc->installRomOsInFlash();
			_log += "new flash with the factory OS (will be saved in " + m_flashPath + ")\n";
		}

		m_midi = std::make_unique<Midi>("G1-Emu");
		if(!m_midi->valid())
		{
			_log += "cannot open the ALSA sequencer\n";
			return false;
		}
		m_pcPort = m_midi->addPort("PC Port");
		m_midiPort = m_midi->addPort("MIDI");
#ifdef G1_BACKEND_JUCE
		if(m_midi->virtualPorts())
			m_stats.midi = "G1-Emu PC Port (editor) and G1-Emu MIDI";
		else
			// Nothing was created, and nothing real was opened either: no editor can reach the G1.
			// Saying which system refused, and what the way round it is, saves the user the hunt.
#ifdef _WIN32
			m_stats.midi = "no virtual MIDI ports: Windows only makes them through Windows MIDI "
				"Services, so no editor can reach the G1 yet (loopMIDI is the usual way round it)";
#else
			m_stats.midi = "no virtual MIDI ports on this system: no editor can reach the G1";
#endif
#else
		m_stats.midi = "G1-Emu:PC Port (editor) and G1-Emu:MIDI, client " + std::to_string(m_midi->clientId());
#endif
		_log += "MIDI ports: " + m_stats.midi + "\n";
		m_rawMidiBound = bindRawMidi(_log);

		// Audio
		if(m_options.audio != "no")
		{
			const float gainDb = m_options.gainDb;
			const float gain = std::pow(10.0f, gainDb / 20.0f);
			char buf[200];
#ifdef G1_BACKEND_JUCE
			const auto device = (m_options.audio == "jack" || m_options.audio == "alsa") ? std::string() : m_options.audio;
			m_juceAudio = std::make_unique<JuceAudio>(device, gain);
			if(m_juceAudio->valid())
				std::snprintf(buf, sizeof(buf), "%s at %u Hz, %zu outputs, %+.0f dB",
					m_juceAudio->deviceName().c_str(), m_juceAudio->rate(), m_juceAudio->outputs(), static_cast<double>(gainDb));
			else
			{
				std::snprintf(buf, sizeof(buf), "no sound: %s", m_juceAudio->error().c_str());
				m_juceAudio.reset();
			}
			if(m_juceAudio)
				m_mc->getDsp(0).setInputProvider([this](int32_t& _l, int32_t& _r) { m_juceAudio->pullInput(_l, _r); });
#else
#ifdef G1_HAVE_JACK
			if(m_options.audio == "jack")
			{
				m_jack = std::make_unique<JackAudio>("G1-Emu", gain, m_options.jackConnect);
				if(m_jack->valid())
				{
					std::snprintf(buf, sizeof(buf), "JACK G1-Emu at %u Hz, %+.0f dB (out_1..4, in_L/R)", m_jack->rate(), static_cast<double>(gainDb));
					m_mc->getDsp(0).setInputProvider([this](int32_t& _l, int32_t& _r) { m_jack->pullInput(_l, _r); });
				}
				else
					m_jack.reset();
			}
			if(!m_jack)
#endif
			{
				const char* dev = (m_options.audio == "alsa" || m_options.audio == "jack") ? "default" : m_options.audio.c_str();
				m_alsa = std::make_unique<AlsaAudio>(dev, gain);
				if(m_alsa->valid())
					std::snprintf(buf, sizeof(buf), "ALSA \"%s\" at 48 kHz, outputs 1/2, %+.0f dB", dev, static_cast<double>(gainDb));
				else
				{
					std::snprintf(buf, sizeof(buf), "cannot open \"%s\": no sound", dev);
					m_alsa.reset();
				}
			}
#endif
			m_stats.audio = buf;
		}
		else
			m_stats.audio = "no audio";
		_log += "audio: " + m_stats.audio + "\n";

		// 4-channel WAV at 96 kHz and 24 bits, only if asked for (G1_RECORD=seconds).
		if(const char* rec = std::getenv("G1_RECORD"))
		{
			m_wavPath = (std::filesystem::path(m_flashPath).parent_path() / "output.wav").string();
			m_wav = std::make_unique<std::ofstream>(m_wavPath, std::ios::binary | std::ios::trunc);
			m_wav->write(std::string(44, '\0').data(), 44);	// header, filled in at the end
			m_wavMaxFrames = static_cast<uint64_t>(std::atof(rec) * 96000.0);
		}

		// One sample per DSP 3 block (96 kHz): the four outputs.
		m_mc->getDsp(3).setBlockCallback([this](const int32_t _o1, const int32_t _o2, const int32_t _o3, const int32_t _o4)
		{
#ifdef G1_BACKEND_JUCE
			if(m_juceAudio)
				m_juceAudio->push(_o1 - g_dc, _o2 - g_dc, _o3 - g_dc, _o4 - g_dc);
#else
			if(m_alsa)
				m_alsa->push(_o1 - g_dc, _o2 - g_dc);
#ifdef G1_HAVE_JACK
			if(m_jack)
				m_jack->push(_o1 - g_dc, _o2 - g_dc, _o3 - g_dc, _o4 - g_dc);
#endif
#endif
			if(m_wav && m_wavFrames < m_wavMaxFrames)
			{
				const int32_t o[4] = {_o1, _o2, _o3, _o4};
				char buf[12];
				for(int c = 0; c < 4; ++c)
				{
					buf[c * 3] = static_cast<char>(o[c] & 0xff);
					buf[c * 3 + 1] = static_cast<char>((o[c] >> 8) & 0xff);
					buf[c * 3 + 2] = static_cast<char>((o[c] >> 16) & 0xff);
				}
				m_wav->write(buf, sizeof(buf));
				++m_wavFrames;
			}
		});

		m_quit = false;
		m_thread = std::thread([this] { run(); });
		return true;
	}

	void EmuHost::stop()
	{
		if(!m_thread.joinable())
			return;
		m_quit = true;
		m_thread.join();
		saveFlash();
		finishWav();
#ifdef G1_BACKEND_JUCE
		m_juceAudio.reset();
#else
		m_jack.reset();
		m_alsa.reset();
#endif
	}

	// Links the emulator to the sound card kept for it, so raw-MIDI programs see the G1 as a
	// MIDI device of their own: Bitwig on Linux reads raw MIDI devices and never looks at an ALSA
	// sequencer port, and only the kernel can make a raw MIDI device.
	//
	// The card's first port carries the notes (the G1's MIDI IN/OUT), which is all a DAW wants;
	// if the card has a second one it gets the PC Port as well. Which card is up to the user:
	// what matters is that it offers as few ports as possible and with a name worth reading, so a
	// one-port USB MIDI gadget (dummy_hcd + g_midi) beats snd-virmidi, which hard-codes sixteen
	// subdevices per device and the name "Virtual Raw MIDI" and floods the DAW's list with them.
	//
	// It is tried again while it fails: the card can be loaded after the emulator.
	bool EmuHost::bindRawMidi(std::string& _log)
	{
#ifdef G1_BACKEND_JUCE
		// Nothing to do: the split between "application ports" and "raw MIDI devices" that makes
		// this necessary is the ALSA sequencer's, and the JUCE backend does not use it. On macOS
		// and Windows a virtual port is a MIDI device like any other.
		(void)_log;
		return false;
#else
		const int card = rawMidiCard(m_options.rawMidiCard);
		if(card < 0)
			return false;
		const auto ports = m_midi->findCardPorts(card);
		if(ports.empty())
			return false;
		auto describe = [card](const AlsaMidi::Port& _p, const char* _ours)
		{
			return std::string(_ours) + " <-> " + (_p.name.empty() ? "card " + std::to_string(card) : _p.name);
		};
		std::string done;
		if(m_midi->link(m_midiPort, ports[0].client, ports[0].port))
			done = describe(ports[0], "MIDI");
		if(ports.size() > 1 && m_midi->link(m_pcPort, ports[1].client, ports[1].port))
			done += (done.empty() ? "" : ", ") + describe(ports[1], "PC Port");
		if(done.empty())
			return false;
		// The warning goes to the log only: the status bar has one line and this would wrap it.
		_log += "raw MIDI: " + done + (ports.size() > 2
			? " (this card publishes " + std::to_string(ports.size()) + " ports; every one of them clutters "
			  "the DAW's MIDI list, see docs/bitwig-midi.md)" : "") + "\n";
		std::lock_guard<std::mutex> lock(m_statsMutex);
		m_stats.rawMidi = done;
		return true;
#endif
	}

	// The level can change while it plays: both backends read it from an atomic on their own thread.
	void EmuHost::setGainDb(const float _gainDb)
	{
		m_options.gainDb = _gainDb;
		const float gain = std::pow(10.0f, _gainDb / 20.0f);
#ifdef G1_BACKEND_JUCE
		if(m_juceAudio)
			m_juceAudio->setGain(gain);
#else
		if(m_alsa)
			m_alsa->setGain(gain);
#ifdef G1_HAVE_JACK
		if(m_jack)
			m_jack->setGain(gain);
#endif
#endif
		std::lock_guard<std::mutex> lock(m_statsMutex);
		const auto at = m_stats.audio.rfind(" dB");
		const auto from = at == std::string::npos ? std::string::npos : m_stats.audio.rfind(' ', at - 1);
		if(from != std::string::npos)
		{
			char buf[16];
			std::snprintf(buf, sizeof(buf), "%+.0f dB", static_cast<double>(_gainDb));
			m_stats.audio.replace(from + 1, at + 3 - from - 1, buf);
		}
	}

	void EmuHost::saveFlash()
	{
		if(!m_mc)
			return;
		std::filesystem::create_directories(std::filesystem::path(m_flashPath).parent_path());
		const auto tmp = m_flashPath + ".tmp";
		{
			std::ofstream f(tmp, std::ios::binary);
			const auto& d = m_mc->getFlash().data();
			f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
		}
		std::filesystem::rename(tmp, m_flashPath);
	}

	void EmuHost::finishWav()
	{
		if(!m_wav)
			return;
		const uint32_t rate = 96000, channels = 4, bytes = 3;
		const auto dataSize = static_cast<uint32_t>(m_wavFrames * channels * bytes);
		auto& w = *m_wav;
		auto u32 = [&](uint32_t v) { w.write(reinterpret_cast<const char*>(&v), 4); };
		auto u16 = [&](uint16_t v) { w.write(reinterpret_cast<const char*>(&v), 2); };
		w.seekp(0);
		w.write("RIFF", 4); u32(36 + dataSize); w.write("WAVEfmt ", 8); u32(16); u16(1); u16(channels);
		u32(rate); u32(rate * channels * bytes); u16(channels * bytes); u16(bytes * 8);
		w.write("data", 4); u32(dataSize);
		m_wav.reset();
	}

	void EmuHost::run()
	{
		auto& mc = *m_mc;
		using clock = std::chrono::steady_clock;
		const auto start = clock::now();
		auto lastStats = start;
		auto lastSave = start;
		auto lastRawMidi = start;
		uint64_t lastStatsCycles = 0;
		double busy = 0;		// seconds the thread has worked since the last statistics
		double lastCpu = processCpuSeconds();
		uint32_t savedProgrammed = mc.getFlash().programmedBytes();
		uint32_t savedErased = mc.getFlash().erasedSectors();

		// Everything coming in through the PC Port is logged so sessions can be replayed without NME.
		std::ofstream pcLog(std::filesystem::path(m_flashPath).parent_path() / "pcport-in.bin", std::ios::binary | std::ios::app);

		std::vector<std::vector<uint8_t>> incoming;
		std::vector<uint8_t> out;

		while(!m_quit)
		{
			const auto t0 = clock::now();

			// What comes in from outside
			m_midi->poll(incoming);
			if(!incoming[m_pcPort].empty())
			{
				m_pcIn += incoming[m_pcPort].size();
				pcLog.write(reinterpret_cast<const char*>(incoming[m_pcPort].data()), static_cast<std::streamsize>(incoming[m_pcPort].size()));
				pcLog.flush();
				mc.getPcPort().receive(incoming[m_pcPort]);
				incoming[m_pcPort].clear();
			}
			if(!incoming[m_midiPort].empty())
			{
				m_midiIn += incoming[m_midiPort].size();
				mc.getSci().write(incoming[m_midiPort]);
				incoming[m_midiPort].clear();
			}

			// Emulate until the real clock is reached (at most 2 ms at once)
			const double elapsed = std::chrono::duration<double>(t0 - start).count();
			const auto target = static_cast<uint64_t>(elapsed * g1::g_ucClock);
			const auto limit = mc.ucCycles() + g1::g_ucClock / 500;
			while(mc.ucCycles() < target && mc.ucCycles() < limit)
				mc.exec();

			// What goes out
			out.clear();
			mc.getPcPort().takeTx(out);
			m_pcOut += out.size();
			m_midi->send(m_pcPort, out);
			out.clear();
			mc.getSci().read(out);
			m_midiOut += out.size();
			m_midi->send(m_midiPort, out);

			const auto t1 = clock::now();
			busy += std::chrono::duration<double>(t1 - t0).count();
			if(mc.ucCycles() >= target)
				std::this_thread::sleep_for(std::chrono::microseconds(500));

			// The snd-virmidi card may be loaded after the emulator: keep trying to take it over.
			if(!m_rawMidiBound && t1 - lastRawMidi >= std::chrono::seconds(2))
			{
				lastRawMidi = t1;
				std::string log;
				m_rawMidiBound = bindRawMidi(log);
				if(m_rawMidiBound)
					std::printf("%s", log.c_str());
			}

			// Save the flash if the G1 wrote to it (patches, settings), at most every 5 s
			if(t1 - lastSave >= std::chrono::seconds(5))
			{
				lastSave = t1;
				const auto prog = mc.getFlash().programmedBytes();
				const auto erased = mc.getFlash().erasedSectors();
				if(prog != savedProgrammed || erased != savedErased)
				{
					saveFlash();
					savedProgrammed = prog;
					savedErased = erased;
				}
			}

			// Statistics, twice per second
			if(t1 - lastStats >= std::chrono::milliseconds(500))
			{
				const double wall = std::chrono::duration<double>(t1 - lastStats).count();
				const double emu = static_cast<double>(mc.ucCycles() - lastStatsCycles) / g1::g_ucClock;
				const double cpu = processCpuSeconds();
				std::lock_guard lock(m_statsMutex);
				m_stats.seconds = elapsed;
				m_stats.speed = 100.0 * emu / wall;
				m_stats.load = 100.0 * busy / wall;
				m_stats.cpuCores = (cpu - lastCpu) / wall;
				for(uint32_t d = 0; d < g1::g_dspCount; ++d)
					m_stats.dspOn[d] = mc.getDsp(d).booted();
				lastCpu = cpu;
				busy = 0;
				lastStats = t1;
				lastStatsCycles = mc.ucCycles();
			}
		}
	}

	EmuHost::Stats EmuHost::stats()
	{
		std::lock_guard lock(m_statsMutex);
		auto s = m_stats;
		s.pcIn = m_pcIn; s.pcOut = m_pcOut; s.midiIn = m_midiIn; s.midiOut = m_midiOut;
#ifdef G1_BACKEND_JUCE
		if(m_juceAudio) { s.peak = m_juceAudio->peak(); s.xruns = m_juceAudio->xruns(); }
#else
		if(m_alsa) { s.peak = m_alsa->peak(); s.xruns = m_alsa->xruns(); }
#ifdef G1_HAVE_JACK
		if(m_jack) { s.peak = m_jack->peak(); s.xruns = m_jack->xruns(); }
#endif
#endif
		return s;
	}

	std::string EmuHost::report()
	{
		const auto s = stats();
		auto& mc = *m_mc;
		char buf[256];
		std::string r;
		std::snprintf(buf, sizeof(buf), "[%6.0fs] speed %5.1f%%  load %3.0f%%  CPU %.1f cores  DSP:", s.seconds, s.speed, s.load, s.cpuCores);
		r += buf;
		for(uint32_t d = 0; d < g1::g_dspCount; ++d)
			r += s.dspOn[d] ? " on" : " --";
		r += "  HI08 words/HC/replies:";
		for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		{
			std::snprintf(buf, sizeof(buf), " %llu/%llu/%llu", static_cast<unsigned long long>(mc.getDsp(d).hostWords()),
				static_cast<unsigned long long>(mc.getDsp(d).hostCommands()), static_cast<unsigned long long>(mc.getDsp(d).wordsToHost()));
			r += buf;
		}
		std::snprintf(buf, sizeof(buf), "  PC Port in/out %llu/%llu  MIDI in/out %llu/%llu\n          audio:",
			static_cast<unsigned long long>(s.pcIn), static_cast<unsigned long long>(s.pcOut),
			static_cast<unsigned long long>(s.midiIn), static_cast<unsigned long long>(s.midiOut));
		r += buf;
		const double wall = s.seconds - m_lastReportTime;
		for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		{
			auto& dsp = mc.getDsp(d);
			const auto frames = dsp.audioFrames();
			std::snprintf(buf, sizeof(buf), "  DSP%u %.0f fr/s", d, wall > 0 ? (frames - m_lastFrames[d]) / wall / 2.0 : 0.0);	// 2 ESSI
			r += buf;
			m_lastFrames[d] = frames;
			const auto& m = dsp.meter();
			for(uint32_t e = 0; e < 2; ++e)
				for(uint32_t sl = 0; sl < g1::Dsp::MeterSlots; ++sl)
					for(uint32_t l = 0; l < g1::Dsp::MeterLines; ++l)
						if(m[e][sl][l] > 256)
						{
							std::snprintf(buf, sizeof(buf), " [E%u s%u tx%u %.0fdB]", e, sl, l, 20.0 * std::log10(m[e][sl][l] / 8388608.0));
							r += buf;
						}
			dsp.resetMeter();
		}
		m_lastReportTime = s.seconds;
		std::snprintf(buf, sizeof(buf), "  output 1/2 %s  dropouts %llu",
			s.peak > 0.0f ? (std::to_string(static_cast<int>(20.0f * std::log10(s.peak))) + " dB").c_str() : "silence",
			static_cast<unsigned long long>(s.xruns));
		r += buf;
		return r;
	}
}
