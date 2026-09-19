// g1run: el Nord Modular G1 emulado, en tiempo real, con puertos MIDI virtuales.
//
//   g1run ROM [FLASH]
//
// ROM   = la ROM de 512 KB del rack (OS 3.03). Nunca entra en el repo.
// FLASH = donde se guarda la flash de 1 MB (OS instalado + patches guardados). Por
//         defecto ~/.local/share/Animatek/G1-Emu/flash.bin. Si no existe, se crea con el
//         OS de fabrica de la ROM, como un G1 recien actualizado.
//
// Crea el cliente ALSA "G1-Emu" con dos puertos, como el aparato:
//   "PC Port" = el PC PORT del editor (NME se conecta aqui)
//   "MIDI"    = el MIDI IN/OUT normal
// Ctrl+C guarda la flash y sale.

#include "alsamidi.h"
#include "g1Lib/g1mc.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

namespace
{
	std::atomic<bool> g_quit{false};

	std::string defaultFlashPath()
	{
		const char* home = std::getenv("HOME");
		return std::string(home ? home : ".") + "/.local/share/Animatek/G1-Emu/flash.bin";
	}

	bool loadFile(const std::string& _path, std::vector<uint8_t>& _data)
	{
		std::ifstream f(_path, std::ios::binary);
		if(!f)
			return false;
		_data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		return true;
	}

	void saveFlash(g1::Microcontroller& _mc, const std::string& _path)
	{
		std::filesystem::create_directories(std::filesystem::path(_path).parent_path());
		const auto tmp = _path + ".tmp";
		{
			std::ofstream f(tmp, std::ios::binary);
			const auto& d = _mc.getFlash().data();
			f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
		}
		std::filesystem::rename(tmp, _path);
	}
}

int main(int argc, char** argv)
{
	if(argc < 2)
	{
		std::fprintf(stderr, "uso: g1run ROM [FLASH]\n");
		return 2;
	}
	std::vector<uint8_t> rom;
	if(!loadFile(argv[1], rom) || rom.size() != g1::g_romSize)
	{
		std::fprintf(stderr, "la ROM debe medir 512 KB\n");
		return 1;
	}
	const std::string flashPath = argc > 2 ? argv[2] : defaultFlashPath();

	g1::Microcontroller mc(rom);
	std::vector<uint8_t> flash;
	if(loadFile(flashPath, flash) && flash.size() == g1::Flash::Size)
	{
		mc.getFlash().data() = flash;
		std::printf("flash cargada de %s\n", flashPath.c_str());
	}
	else
	{
		mc.installRomOsInFlash();
		std::printf("flash nueva con el OS de fabrica (se guardara en %s)\n", flashPath.c_str());
	}

	g1app::AlsaMidi midi("G1-Emu");
	if(!midi.valid())
	{
		std::fprintf(stderr, "no puedo abrir el secuenciador ALSA\n");
		return 1;
	}
	const int pcPort = midi.addPort("PC Port");
	const int midiPort = midi.addPort("MIDI");
	std::printf("puertos MIDI: %d:%d \"G1-Emu PC Port\" (editor) y %d:%d \"G1-Emu MIDI\"\n",
		midi.clientId(), 0, midi.clientId(), 1);
	std::fflush(stdout);

	std::signal(SIGINT, [](int) { g_quit = true; });
	std::signal(SIGTERM, [](int) { g_quit = true; });

	using clock = std::chrono::steady_clock;
	const auto start = clock::now();
	auto lastReport = start;
	uint64_t lastReportCycles = 0;
	uint32_t savedProgrammed = mc.getFlash().programmedBytes();
	uint32_t savedErased = mc.getFlash().erasedSectors();
	uint64_t pcIn = 0, pcOut = 0, midiIn = 0, midiOut = 0;

	// Todo lo que entra por el PC Port se apunta para poder reproducir sesiones sin NME.
	std::ofstream pcLog(std::filesystem::path(flashPath).parent_path() / "pcport-in.bin", std::ios::binary | std::ios::app);
	std::vector<uint64_t> lastFrames(g1::g_dspCount, 0);

	// La salida del DSP 3 (ESSI0 = salidas 1/2, ESSI1 = 3/4) se puede grabar en un WAV de 4
	// canales a 96 kHz y 24 bits: G1_RECORD=10 ./g1.sh graba los primeros 10 s.
	const auto wavPath = std::filesystem::path(flashPath).parent_path() / "salida.wav";
	std::ofstream wav;
	if(std::getenv("G1_RECORD"))
	{
		wav.open(wavPath, std::ios::binary | std::ios::trunc);
		wav.write(std::string(44, '\0').data(), 44);	// cabecera, se rellena al salir
	}
	uint64_t wavFrames = 0;
	std::array<int32_t, 4> wavFrame{};
	// Solo se graba si se pide con G1_RECORD=segundos (tope; por defecto no se graba nada).
	const char* recordEnv = std::getenv("G1_RECORD");
	const uint64_t wavMaxFrames = recordEnv ? static_cast<uint64_t>(std::atof(recordEnv) * 96000.0) : 0;
	mc.getDsp(3).setAudioCallback([&](const uint32_t _essi, const int32_t _l, const int32_t _r)
	{
		if(wavFrames >= wavMaxFrames)
			return;
		wavFrame[_essi * 2] = _l;
		wavFrame[_essi * 2 + 1] = _r;
		if(_essi != 1)	// se escribe la trama al completar el ESSI1
			return;
		char buf[12];
		for(int c = 0; c < 4; ++c)
		{
			buf[c * 3] = static_cast<char>(wavFrame[c] & 0xff);
			buf[c * 3 + 1] = static_cast<char>((wavFrame[c] >> 8) & 0xff);
			buf[c * 3 + 2] = static_cast<char>((wavFrame[c] >> 16) & 0xff);
		}
		wav.write(buf, sizeof(buf));
		++wavFrames;
	});

	std::vector<std::vector<uint8_t>> incoming;
	std::vector<uint8_t> out;

	while(!g_quit)
	{
		// Lo que llega de fuera
		midi.poll(incoming);
		if(!incoming[pcPort].empty())
		{
			pcIn += incoming[pcPort].size();
			pcLog.write(reinterpret_cast<const char*>(incoming[pcPort].data()), static_cast<std::streamsize>(incoming[pcPort].size()));
			pcLog.flush();
			mc.getPcPort().receive(incoming[pcPort]);
			incoming[pcPort].clear();
		}
		if(!incoming[midiPort].empty())
		{
			midiIn += incoming[midiPort].size();
			mc.getSci().write(incoming[midiPort]);
			incoming[midiPort].clear();
		}

		// Emular hasta alcanzar el reloj real (como mucho 2 ms de golpe)
		const auto now = clock::now();
		const double elapsed = std::chrono::duration<double>(now - start).count();
		const auto target = static_cast<uint64_t>(elapsed * g1::g_ucClock);
		const auto limit = mc.ucCycles() + g1::g_ucClock / 500;
		while(mc.ucCycles() < target && mc.ucCycles() < limit)
			mc.exec();

		// Lo que sale
		out.clear();
		mc.getPcPort().takeTx(out);
		pcOut += out.size();
		midi.send(pcPort, out);
		out.clear();
		mc.getSci().read(out);
		midiOut += out.size();
		midi.send(midiPort, out);

		if(mc.ucCycles() >= target)
			std::this_thread::sleep_for(std::chrono::microseconds(500));

		// Guardar la flash si el G1 ha escrito en ella (patches, ajustes)
		const auto prog = mc.getFlash().programmedBytes();
		const auto erased = mc.getFlash().erasedSectors();

		if(now - lastReport >= std::chrono::seconds(5))
		{
			const double wall = std::chrono::duration<double>(now - lastReport).count();
			const double emu = static_cast<double>(mc.ucCycles() - lastReportCycles) / g1::g_ucClock;
			std::printf("[%6.0fs] velocidad %5.1f%%  DSP:", elapsed, 100.0 * emu / wall);
			for(uint32_t d = 0; d < g1::g_dspCount; ++d)
				std::printf(" %s", mc.getDsp(d).booted() ? "on" : "--");
			std::printf("  HI08 palabras/HC/respuestas:");
			for(uint32_t d = 0; d < g1::g_dspCount; ++d)
				std::printf(" %llu/%llu/%llu", static_cast<unsigned long long>(mc.getDsp(d).hostWords()),
					static_cast<unsigned long long>(mc.getDsp(d).hostCommands()), static_cast<unsigned long long>(mc.getDsp(d).wordsToHost()));
			std::printf("  PC Port in/out %llu/%llu  MIDI in/out %llu/%llu\n",
				static_cast<unsigned long long>(pcIn), static_cast<unsigned long long>(pcOut),
				static_cast<unsigned long long>(midiIn), static_cast<unsigned long long>(midiOut));
			// Audio: tramas por segundo y picos por DSP / ESSI / slot / linea TX
			std::printf("          audio:");
			for(uint32_t d = 0; d < g1::g_dspCount; ++d)
			{
				auto& dsp = mc.getDsp(d);
				const auto frames = dsp.audioFrames();
				std::printf("  DSP%u %.0f tr/s", d, (frames - lastFrames[d]) / wall / 2.0);	// 2 ESSI
				lastFrames[d] = frames;
				const auto& m = dsp.meter();
				for(uint32_t e = 0; e < 2; ++e)
					for(uint32_t sl = 0; sl < g1::Dsp::MeterSlots; ++sl)
						for(uint32_t l = 0; l < g1::Dsp::MeterLines; ++l)
							if(m[e][sl][l] > 256)
								std::printf(" [E%u s%u tx%u %.0fdB]", e, sl, l, 20.0 * std::log10(m[e][sl][l] / 8388608.0));
				dsp.resetMeter();
			}
			std::printf("\n");
			std::fflush(stdout);
			lastReport = now;
			lastReportCycles = mc.ucCycles();

			if(prog != savedProgrammed || erased != savedErased)
			{
				saveFlash(mc, flashPath);
				savedProgrammed = prog;
				savedErased = erased;
			}
		}
	}

	saveFlash(mc, flashPath);
	std::printf("\nflash guardada en %s\n", flashPath.c_str());

	// Cabecera WAV: PCM, 4 canales, 96 kHz, 24 bits
	if(wav.is_open())
	{
		const uint32_t rate = 96000, channels = 4, bytes = 3;
		const uint32_t dataSize = static_cast<uint32_t>(wavFrames * channels * bytes);
		auto u32 = [&](uint32_t v) { wav.write(reinterpret_cast<const char*>(&v), 4); };
		auto u16 = [&](uint16_t v) { wav.write(reinterpret_cast<const char*>(&v), 2); };
		wav.seekp(0);
		wav.write("RIFF", 4); u32(36 + dataSize); wav.write("WAVEfmt ", 8); u32(16); u16(1); u16(channels);
		u32(rate); u32(rate * channels * bytes); u16(channels * bytes); u16(bytes * 8);
		wav.write("data", 4); u32(dataSize);
		std::printf("audio grabado en %s (%.1f s)\n", wavPath.c_str(), wavFrames / 96000.0);
	}
	return 0;
}
