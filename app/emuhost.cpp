#include "emuhost.h"

#include "alsaaudio.h"
#include "alsamidi.h"
#ifdef G1_HAVE_JACK
#include "jackaudio.h"
#else
namespace g1app { class JackAudio {}; }
#endif

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <unistd.h>

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

		// Segundos de CPU (usuario + sistema) de todo el proceso.
		double processCpuSeconds()
		{
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
		}

		// Las salidas llevan el desplazamiento X:$5F del DSP 3 ($155, casi nada); en el aparato lo
		// quita el condensador de salida. Se resta para no mandar continua a la tarjeta.
		constexpr int32_t g_dc = 0x155;
	}

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

	bool EmuHost::start(const std::string& _romPath, const std::string& _flashPath, std::string& _log)
	{
		std::vector<uint8_t> rom;
		if(!loadFile(_romPath, rom) || rom.size() != g1::g_romSize)
		{
			_log += "la ROM debe medir 512 KB: " + _romPath + "\n";
			return false;
		}
		m_flashPath = _flashPath.empty() ? defaultFlashPath() : _flashPath;
		m_mc = std::make_unique<g1::Microcontroller>(rom);

		std::vector<uint8_t> flash;
		if(loadFile(m_flashPath, flash) && flash.size() == g1::Flash::Size)
		{
			m_mc->getFlash().data() = flash;
			_log += "flash cargada de " + m_flashPath + "\n";
		}
		else
		{
			m_mc->installRomOsInFlash();
			_log += "flash nueva con el OS de fabrica (se guardara en " + m_flashPath + ")\n";
		}

		m_midi = std::make_unique<AlsaMidi>("G1-Emu");
		if(!m_midi->valid())
		{
			_log += "no puedo abrir el secuenciador ALSA\n";
			return false;
		}
		m_pcPort = m_midi->addPort("PC Port");
		m_midiPort = m_midi->addPort("MIDI");
		m_stats.midi = "G1-Emu:PC Port (editor) y G1-Emu:MIDI, cliente " + std::to_string(m_midi->clientId());
		_log += "puertos MIDI: " + m_stats.midi + "\n";

		// Audio
		const char* audioDev = std::getenv("G1_AUDIO");
		if(!audioDev || std::string(audioDev) != "no")
		{
			const char* gainEnv = std::getenv("G1_GAIN_DB");
			const float gainDb = gainEnv ? static_cast<float>(std::atof(gainEnv)) : 36.0f;
			const float gain = std::pow(10.0f, gainDb / 20.0f);
			char buf[160];
#ifdef G1_HAVE_JACK
			if(!audioDev || std::string(audioDev) == "jack")
			{
				m_jack = std::make_unique<JackAudio>("G1-Emu", gain);
				if(m_jack->valid())
				{
					std::snprintf(buf, sizeof(buf), "JACK G1-Emu a %u Hz, %+.0f dB (out_1..4, in_L/R)", m_jack->rate(), gainDb);
					m_mc->getDsp(0).setInputProvider([this](int32_t& _l, int32_t& _r) { m_jack->pullInput(_l, _r); });
				}
				else
					m_jack.reset();
			}
			if(!m_jack)
#endif
			{
				const char* dev = (!audioDev || std::string(audioDev) == "alsa" || std::string(audioDev) == "jack") ? "default" : audioDev;
				m_alsa = std::make_unique<AlsaAudio>(dev, gain);
				if(m_alsa->valid())
					std::snprintf(buf, sizeof(buf), "ALSA \"%s\" a 48 kHz, salidas 1/2, %+.0f dB", dev, gainDb);
				else
				{
					std::snprintf(buf, sizeof(buf), "no puedo abrir \"%s\": sin sonido", dev);
					m_alsa.reset();
				}
			}
			m_stats.audio = buf;
		}
		else
			m_stats.audio = "sin audio (G1_AUDIO=no)";
		_log += "audio: " + m_stats.audio + "\n";

		// WAV de 4 canales a 96 kHz y 24 bits, solo si se pide (G1_RECORD=segundos).
		if(const char* rec = std::getenv("G1_RECORD"))
		{
			m_wavPath = (std::filesystem::path(m_flashPath).parent_path() / "salida.wav").string();
			m_wav = std::make_unique<std::ofstream>(m_wavPath, std::ios::binary | std::ios::trunc);
			m_wav->write(std::string(44, '\0').data(), 44);	// cabecera, se rellena al acabar
			m_wavMaxFrames = static_cast<uint64_t>(std::atof(rec) * 96000.0);
		}

		// Una muestra por bloque del DSP 3 (96 kHz): las cuatro salidas.
		m_mc->getDsp(3).setBlockCallback([this](const int32_t _o1, const int32_t _o2, const int32_t _o3, const int32_t _o4)
		{
			if(m_alsa)
				m_alsa->push(_o1 - g_dc, _o2 - g_dc);
#ifdef G1_HAVE_JACK
			if(m_jack)
				m_jack->push(_o1 - g_dc, _o2 - g_dc, _o3 - g_dc, _o4 - g_dc);
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
		m_jack.reset();
		m_alsa.reset();
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
		uint64_t lastStatsCycles = 0;
		double busy = 0;		// segundos que el hilo ha trabajado desde las ultimas estadisticas
		double lastCpu = processCpuSeconds();
		uint32_t savedProgrammed = mc.getFlash().programmedBytes();
		uint32_t savedErased = mc.getFlash().erasedSectors();

		// Todo lo que entra por el PC Port se apunta para poder reproducir sesiones sin NME.
		std::ofstream pcLog(std::filesystem::path(m_flashPath).parent_path() / "pcport-in.bin", std::ios::binary | std::ios::app);

		std::vector<std::vector<uint8_t>> incoming;
		std::vector<uint8_t> out;

		while(!m_quit)
		{
			const auto t0 = clock::now();

			// Lo que llega de fuera
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

			// Emular hasta alcanzar el reloj real (como mucho 2 ms de golpe)
			const double elapsed = std::chrono::duration<double>(t0 - start).count();
			const auto target = static_cast<uint64_t>(elapsed * g1::g_ucClock);
			const auto limit = mc.ucCycles() + g1::g_ucClock / 500;
			while(mc.ucCycles() < target && mc.ucCycles() < limit)
				mc.exec();

			// Lo que sale
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

			// Guardar la flash si el G1 ha escrito en ella (patches, ajustes), como mucho cada 5 s
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

			// Estadisticas, dos veces por segundo
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
		if(m_alsa) { s.peak = m_alsa->peak(); s.xruns = m_alsa->xruns(); }
#ifdef G1_HAVE_JACK
		if(m_jack) { s.peak = m_jack->peak(); s.xruns = m_jack->xruns(); }
#endif
		return s;
	}

	std::string EmuHost::report()
	{
		const auto s = stats();
		auto& mc = *m_mc;
		char buf[256];
		std::string r;
		std::snprintf(buf, sizeof(buf), "[%6.0fs] velocidad %5.1f%%  carga %3.0f%%  CPU %.1f nucleos  DSP:", s.seconds, s.speed, s.load, s.cpuCores);
		r += buf;
		for(uint32_t d = 0; d < g1::g_dspCount; ++d)
			r += s.dspOn[d] ? " on" : " --";
		r += "  HI08 palabras/HC/respuestas:";
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
			std::snprintf(buf, sizeof(buf), "  DSP%u %.0f tr/s", d, wall > 0 ? (frames - m_lastFrames[d]) / wall / 2.0 : 0.0);	// 2 ESSI
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
		std::snprintf(buf, sizeof(buf), "  salida 1/2 %s  cortes %llu",
			s.peak > 0.0f ? (std::to_string(static_cast<int>(20.0f * std::log10(s.peak))) + " dB").c_str() : "silencio",
			static_cast<unsigned long long>(s.xruns));
		r += buf;
		return r;
	}
}
