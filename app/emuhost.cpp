#include "emuhost.h"
#include <ctime>

#include "diaglog.h"
#include "hostlog.h"
#include "romfinder.h"

#include "g1Lib/g1thread.h"

#include "miditransport.h"

#ifdef G1_BACKEND_JUCE
#include "juceaudio.h"
#else
#include "alsaaudio.h"
#ifdef G1_HAVE_JACK
#include "jackaudio.h"
#else
namespace g1app { class JackAudio {}; }
#endif
#endif

#include <algorithm>
#include <array>
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

		// Subtracted so no DC reaches the sound card (see Engine::OutputDc).
		constexpr int32_t g_dc = Engine::OutputDc;
	}

	EmuHost::EmuHost() = default;

	EmuHost::~EmuHost()
	{
		stop();
	}

	bool EmuHost::start(const std::string& _romPath, const std::string& _flashPath, std::string& _log)
	{
		m_options.load(defaultSettingsPath());
		m_selectedControllers = m_options.selectedControllers;
		m_octaveShift = m_options.octaveShift;

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
		if(const char* v = std::getenv("G1_DIRECT_LINK"))
			m_options.directLink = std::string(v) != "0";
		if(const char* v = std::getenv("G1_RAWMIDI"))
			m_options.rawMidiCard = std::string(v) == "0" ? "" : v;
		if(const char* v = std::getenv("G1_PCPORT_OUT"))
			m_options.pcPortOutDevice = v;
		if(const char* v = std::getenv("G1_PCPORT_IN"))
			m_options.pcPortInDevice = v;
		if(const char* v = std::getenv("G1_MIDI_OUT"))
			m_options.midiOutDevice = v;
		if(const char* v = std::getenv("G1_MIDI_IN"))
			m_options.midiInDevice = v;

		// The ROM. G1-Emu ships none, so not finding one is the normal first run, not a crash:
		// the message has to say what to put where, and what was wrong with what is already there.
		m_romProblem.clear();
		const auto search = findRom(_romPath, m_options.rom);
		std::vector<uint8_t> rom;
		if(!search.found() || !inspectRom(search.path, rom).ok())
		{
			const auto msg = missingRomMessage(search);
			m_romProblem = msg;
			_log += msg;
			logEvent("start: " + msg);
			return false;
		}
		m_options.rom = search.path;
		_log += "ROM: " + search.path + "\n";
		m_rom = std::move(rom);

		m_flashPath = _flashPath.empty() ? defaultFlashPath() : _flashPath;
		const char* up = std::getenv("G1_UPDATE");
		boot(up && std::string(up) == "1", _log);

		m_midi = makeMidiTransport("G1-Emu", _log);
		if(!m_midi)
			return false;
		m_pcPort = m_midi->addPort("PC Port", {m_options.pcPortOutDevice, m_options.pcPortInDevice});
		m_midiPort = m_midi->addPort("MIDI", {m_options.midiOutDevice, m_options.midiInDevice});
		m_stats.midi = m_midi->describe();
		_log += "MIDI ports: " + m_stats.midi + "\n";

		// The direct link for Animatek NME, beside the PC Port (issue #8). The first instance is
		// "G1-Emu", the next ones carry their number, like their MIDI ports.
		if(m_options.directLink)
		{
			std::string linkError;
			if(m_link.start("G1-Emu", linkError))
			{
				const int n = m_link.port() - DirectLink::kBasePort;
				if(n > 0)
					m_link.setName("G1-Emu " + std::to_string(n + 1));
				m_link.setPcPortIds(m_midi->portIds(m_pcPort));
				_log += m_link.describe() + "\n";
			}
			else
				_log += "direct link: " + linkError + "\n";
		}
		m_rawMidiBound = bindRawMidi(_log);

		// Audio
		if(m_options.audio != "no")
		{
			const float gainDb = m_options.gainDb;
			const float gain = std::pow(10.0f, gainDb / 20.0f);
			char buf[200];
#ifdef G1_BACKEND_JUCE
			const auto selected = (m_options.audio == "jack" || m_options.audio == "alsa") ? std::string() : m_options.audio;
			const auto separator = selected.find(": ");
			const auto type = separator == std::string::npos ? std::string() : selected.substr(0, separator);
			const auto device = separator == std::string::npos ? selected : selected.substr(separator + 2);
			m_juceAudio = std::make_unique<JuceAudio>(device, gain, type);
			if(m_juceAudio->valid())
				std::snprintf(buf, sizeof(buf), "%s at %u Hz, %zu outputs, %+.0f dB",
					m_juceAudio->deviceName().c_str(), m_juceAudio->rate(), m_juceAudio->outputs(), static_cast<double>(gainDb));
			else
			{
				std::snprintf(buf, sizeof(buf), "no sound: %s", m_juceAudio->error().c_str());
				m_juceAudio.reset();
			}
#else
#ifdef G1_HAVE_JACK
			if(m_options.audio == "jack")
			{
				m_jack = std::make_unique<JackAudio>("G1-Emu", gain, m_options.jackConnect);
				if(m_jack->valid())
					std::snprintf(buf, sizeof(buf), "JACK G1-Emu at %u Hz, %+.0f dB (out_1..4, in_L/R)", m_jack->rate(), static_cast<double>(gainDb));
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

		logEvent("start:\n" + _log);
		launch();
		return true;
	}

	// The G1 itself, from the ROM kept and the flash on disk. _update starts it in the boot ROM's
	// update mode.
	void EmuHost::boot(const bool _update, std::string& _log)
	{
		std::string osNote;
		const auto os = m_options.loadOs(osNote);
		if(!osNote.empty())
			_log += osNote + "\n";
		m_engine = std::make_unique<Engine>(m_rom, os, m_options.dspCount());
		_log += std::string("model: ") + HostOptions::modelName(m_engine->mc().dspCount()) + " (" + std::to_string(m_engine->mc().dspCount()) + " DSPs)\n";

		std::vector<uint8_t> flash;
		if(loadFile(m_flashPath, flash) && m_engine->loadFlash(flash))
			_log += "flash loaded from " + m_flashPath + "\n";
		else
			_log += std::string("new flash with ") + (m_engine->customOs() ? "the OS image" : "the factory OS") + " (will be saved in " + m_flashPath + ")\n";
		m_updateMode = _update;
		if(_update)
		{
			// The loader finds no OS (length $FFFFFFFF) and runs its Update utility, which waits for
			// an update (Clavia's updater sends it to the PC Port). Only the length goes: the banks stay, and so does the OS until
			// the updater replaces it.
			auto& image = m_engine->mc().getFlash().data();
			std::fill(image.begin() + 8, image.begin() + 12, uint8_t(0xff));
			_log += "UPDATE MODE: the G1 waits for an OS update. Run Clavia's updater, choose this G1's PC Port as\n"
				"its MIDI output and input, and close G1-Emu when the display says the update is complete.\n";
		}
	}

	// Switching it off and on: the flash is saved and a new G1 boots from it, with the knobs where
	// they were. The MIDI ports and the sound card stay open, so an editor keeps its connection.
	// Never in update mode: a restart after an update is the first boot of the OS that came in.
	bool EmuHost::restart(std::string& _log)
	{
		if(!running())
			return false;
		m_quit = true;
		m_thread.join();
		if(m_updateMode)
			keepReceivedOs();
		saveFlash();

		std::array<uint8_t, 256> adc{};
		for(size_t i = 0; i < adc.size(); ++i)
			adc[i] = m_engine->mc().adc(static_cast<uint8_t>(i));
		m_engine.reset();
		boot(false, _log);
		for(size_t i = 0; i < adc.size(); ++i)
			m_engine->mc().setAdc(static_cast<uint8_t>(i), adc[i]);

		m_synthSettings.reset();
		m_presets.reset();
		{
			std::lock_guard<std::mutex> lock(m_statsMutex);
			m_stats.dspProblem.clear();
		}
		std::fill(m_lastFrames.begin(), m_lastFrames.end(), 0);
		m_lastReportTime = 0;
		m_lastReportCycles = 0;

		logEvent("restart");
		launch();
		return true;
	}

	// The G1 (a new one, or the first) connected and running.
	void EmuHost::launch()
	{
		wireEngine();
		logEvent(systemSummary(m_engine->mc().threaded()));
		m_quit = false;
		m_thread = std::thread([this] { run(); });
	}

	// What connects a new G1 to the sound card: the input to DSP 0, the four outputs from the last DSP
	// (3 on a Regular, 7 on an Expanded).
	void EmuHost::wireEngine()
	{
		auto& mc = m_engine->mc();
#ifdef G1_BACKEND_JUCE
		if(m_juceAudio)
			mc.getDsp(0).setInputProvider([this](int32_t& _l, int32_t& _r) { m_juceAudio->pullInput(_l, _r); });
#elif defined(G1_HAVE_JACK)
		if(m_jack)
			mc.getDsp(0).setInputProvider([this](int32_t& _l, int32_t& _r) { m_jack->pullInput(_l, _r); });
#endif

		// One sample per block of the last DSP (96 kHz): the four outputs.
		mc.outputDsp().setBlockCallback([this](const int32_t _o1, const int32_t _o2, const int32_t _o3, const int32_t _o4)
		{
			m_health.output(_o1 - g_dc, _o2 - g_dc);
			// Raced ahead, the boot's frames would fill the card's queue past its cushion, and the
			// trimming leaves it four times as deep: that much latency for good.
			if(m_racing.load(std::memory_order_relaxed))
				return;
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
	}

	void EmuHost::stop()
	{
		if(!m_thread.joinable())
			return;
		m_quit = true;
		m_thread.join();
		m_link.stop();   // the editor sees the link close, as when the emulator is gone
		if(m_updateMode)
			keepReceivedOs();
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
		std::string summary;
		if(!m_midi->linkRawCard(m_options.rawMidiCard, m_pcPort, m_midiPort, _log, summary))
			return false;
		std::lock_guard<std::mutex> lock(m_statsMutex);
		m_stats.rawMidi = summary;
		return true;
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

	void EmuHost::keepReceivedOs()
	{
		const auto& flash = m_engine->mc().getFlash().data();
		const uint32_t len = (uint32_t(flash[8]) << 24) | (uint32_t(flash[9]) << 16) | (uint32_t(flash[10]) << 8) | flash[11];
		if(len == 0xffffffff || len < 0x10000 || len + 4 > Engine::OsBytes - 0x20 || ((len + 4) & 3))
		{
			std::fprintf(stderr, "update mode: no complete OS came in (length $%08x); nothing changes\n", len);
			return;
		}
		// The loader copies (length >> 2) + 1 long words, one more than Clavia's updater means: it
		// stores the image's size, so that last long word is erased flash. It is left out.
		std::vector<uint8_t> os(flash.begin() + 0x20, flash.begin() + 0x20 + len + 4);
		if(os.size() >= 4 && std::all_of(os.end() - 4, os.end(), [](const uint8_t _b) { return _b == 0xff; }))
			os.resize(os.size() - 4);
		char stamp[32];
		const auto now = std::time(nullptr);
		std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", std::localtime(&now));
		const auto dir = std::filesystem::path(m_flashPath).parent_path() / "os";
		std::filesystem::create_directories(dir);
		const auto path = (dir / ("received-" + std::string(stamp) + ".bin")).string();
		{
			std::ofstream f(path, std::ios::binary);
			f.write(reinterpret_cast<const char*>(os.data()), static_cast<std::streamsize>(os.size()));
		}
		HostOptions options;
		options.load(defaultSettingsPath());
		options.os = path;
		options.save(defaultSettingsPath());
		m_options.os = path;	// a restart boots it, and the window's next save keeps it
		std::fprintf(stderr, "update mode: an OS of %zu bytes came in; kept as %s and set as os in the settings\n", os.size(), path.c_str());
	}

	void EmuHost::saveFlash()
	{
		if(!m_engine)
			return;
		std::filesystem::create_directories(std::filesystem::path(m_flashPath).parent_path());
		const auto tmp = m_flashPath + ".tmp";
		{
			std::ofstream f(tmp, std::ios::binary);
			const auto& d = m_engine->mc().getFlash().data();
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
		auto& mc = m_engine->mc();
		g1::keepThreadAtFullSpeed();
		using clock = std::chrono::steady_clock;
		auto start = clock::now();
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
		std::vector<uint8_t> routed;	// the MIDI port's bytes, the notes moved by their slot's octave shift
		std::vector<uint8_t> out, toEditor, toG1, linkIn;
		constexpr uint64_t msCycles = g1::g_ucClock / 1000;	// the G1's own time, for the settings link

		// G1_MIDI_LOG=1: every chunk in and out of both ports, with its first bytes. The G1's
		// protocol is all SysEx, so "F0 33 ..." arriving and nothing going back says more in one
		// line than any amount of guessing about whose side the silence is on.
		const bool midiLog = [] { const char* v = std::getenv("G1_MIDI_LOG"); return v && *v && *v != '0'; }();
		const auto logMidi = [midiLog](const char* _dir, const char* _port, const std::vector<uint8_t>& _bytes)
		{
			if(!midiLog || _bytes.empty())
				return;
			std::string hex;
			for(size_t i = 0; i < _bytes.size() && i < 16; ++i)
			{
				char b[4];
				std::snprintf(b, sizeof(b), "%02x ", _bytes[i]);
				hex += b;
			}
			std::printf("[midi] %s %-7s %3zu bytes: %s%s\n", _dir, _port, _bytes.size(), hex.c_str(),
				_bytes.size() > 16 ? "..." : "");
			std::fflush(stdout);
		};

		// The boot races: the G1 runs as fast as it goes until the OS answers the settings link, and
		// keeps real time from there. Nothing can be heard before. G1_FAST_BOOT=0 boots in real time.
		const bool fastBoot = [] { const char* v = std::getenv("G1_FAST_BOOT"); return !(v && *v == '0'); }();
		bool osUp = false;

		while(!m_quit)
		{
			const auto t0 = clock::now();

			// What comes in from outside
			m_midi->poll(incoming);
			if(!incoming[m_pcPort].empty())
			{
				m_pcIn += incoming[m_pcPort].size();
				logMidi("in ", "PC Port", incoming[m_pcPort]);
				pcLog.write(reinterpret_cast<const char*>(incoming[m_pcPort].data()), static_cast<std::streamsize>(incoming[m_pcPort].size()));
				pcLog.flush();
				mc.getPcPort().receive(incoming[m_pcPort]);
				m_synthSettings.editorSent(incoming[m_pcPort], mc.ucCycles() / msCycles);
				m_presets.editorSent(incoming[m_pcPort], mc.ucCycles() / msCycles);
				incoming[m_pcPort].clear();
			}
			// The editor on the direct link talks to the same PC Port.
			linkIn.clear();
			m_link.poll(linkIn);
			if(!linkIn.empty())
			{
				m_pcIn += linkIn.size();
				logMidi("in ", "Link", linkIn);
				mc.getPcPort().receive(linkIn);
				m_synthSettings.editorSent(linkIn, mc.ucCycles() / msCycles);
				m_presets.editorSent(linkIn, mc.ucCycles() / msCycles);
			}
			if(!incoming[m_midiPort].empty())
			{
				m_midiIn += incoming[m_midiPort].size();
				logMidi("in ", "MIDI", incoming[m_midiPort]);
				// Each note moved by its slot's octave shift, which the OS keeps but does not apply.
				routed.clear();
				m_midiRouting.apply(mc, MidiRouting::layout(&m_synthSettings, m_selectedControllers, m_octaveShift), incoming[m_midiPort].data(), incoming[m_midiPort].size(), routed);
				mc.getSci().write(routed);
				// A note on with a velocity, the way it arrives (running status aside).
				const auto& in = incoming[m_midiPort];
				for(size_t i = 0; i + 2 < in.size(); ++i)
					if((in[i] & 0xf0) == 0x90 && in[i + 2] != 0 && !(in[i + 1] & 0x80))
					{
						m_health.midiIn(mc, mc.ucCycles() / msCycles);
						break;
					}
				incoming[m_midiPort].clear();
			}

			// Emulate until the real clock is reached (at most 2 ms at once)
			const double elapsed = std::chrono::duration<double>(t0 - start).count();
			const bool racing = fastBoot && !osUp;
			m_racing.store(racing, std::memory_order_relaxed);
			const auto limit = mc.ucCycles() + g1::g_ucClock / 500;
			const auto target = racing ? limit : static_cast<uint64_t>(elapsed * g1::g_ucClock);
			while(mc.ucCycles() < target && mc.ucCycles() < limit)
				mc.exec();
			if(!osUp)
			{
				SynthSettings s;
				uint64_t revision = 0;
				if(m_synthSettings.settings(s, revision))
				{
					osUp = true;
					// From here on, real time again, as if the G1 had been switched on that long ago.
					if(fastBoot)
						start = clock::now() - std::chrono::duration_cast<clock::duration>(
						std::chrono::duration<double>(static_cast<double>(mc.ucCycles()) / g1::g_ucClock));
				}
			}

			// What goes out. The answers to the settings' and the presets' own requests stay here.
			const auto nowMs = mc.ucCycles() / msCycles;
			m_health.tick(mc, nowMs);
			out.clear();
			mc.getPcPort().takeTx(out);
			toEditor.clear();
			m_synthSettings.g1Sent(out, nowMs, toEditor);
			out.swap(toEditor);
			toEditor.clear();
			m_presets.g1Sent(out, nowMs, toEditor);
			out.swap(toEditor);
			// Their own requests, each taken by the other for an editor's, so they never talk at once.
			if(!m_updateMode)	// Clavia's updater has the PC Port to itself
			{
				toG1.clear();
				m_synthSettings.tick(nowMs, toG1);
				if(!toG1.empty())
				{
					mc.getPcPort().receive(toG1);
					m_presets.editorSent(toG1, nowMs);
				}
				toG1.clear();
				m_presets.tick(nowMs, toG1);
				if(!toG1.empty())
				{
					mc.getPcPort().receive(toG1);
					m_synthSettings.editorSent(toG1, nowMs);
				}
			}
			m_pcOut += out.size();
			logMidi("out", "PC Port", out);
			m_midi->send(m_pcPort, out);
			m_link.send(out);   // and hears the same replies
			out.clear();
			mc.getSci().read(out);
			m_midiOut += out.size();
			logMidi("out", "MIDI", out);
			m_midi->send(m_midiPort, out);

			const auto t1 = clock::now();
			busy += std::chrono::duration<double>(t1 - t0).count();
			if(!racing && mc.ucCycles() >= target)
			{
				mc.setIdle(true);	// the DSP threads sleep too instead of spinning through it
				g1::preciseSleep(std::chrono::microseconds(500));	// sleep_for: 15.6 ms on Windows
				mc.setIdle(false);
			}

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
				m_stats.dspCount = mc.dspCount();
				for(uint32_t d = 0; d < mc.dspCount(); ++d)
				{
					m_stats.dspOn[d] = mc.getDsp(d).booted();
					m_stats.dspFailed[d] = mc.getDsp(d).jitFailed();
					if(mc.getDsp(d).jitFailed() && m_stats.dspProblem.empty())
					{
						m_stats.dspProblem = "DSP " + std::to_string(d) + " stopped: the JIT could not generate its code (" + mc.getDsp(d).jitFailure() + ")";
						logEvent(m_stats.dspProblem);
					}
				}
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
		auto& mc = m_engine->mc();
		char buf[256];
		std::string r;
		std::snprintf(buf, sizeof(buf), "[%6.0fs] speed %5.1f%%  load %3.0f%%  CPU %.1f cores  DSP:", s.seconds, s.speed, s.load, s.cpuCores);
		r += buf;
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
			r += s.dspFailed[d] ? " XX" : s.dspOn[d] ? " on" : " --";
		r += "  HI08 words/HC/replies:";
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
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
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
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
