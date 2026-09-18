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

#include <atomic>
#include <chrono>
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

	std::vector<std::vector<uint8_t>> incoming;
	std::vector<uint8_t> out;

	while(!g_quit)
	{
		// Lo que llega de fuera
		midi.poll(incoming);
		if(!incoming[pcPort].empty())
		{
			pcIn += incoming[pcPort].size();
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
			std::printf("  PC Port in/out %llu/%llu  MIDI in/out %llu/%llu\n",
				static_cast<unsigned long long>(pcIn), static_cast<unsigned long long>(pcOut),
				static_cast<unsigned long long>(midiIn), static_cast<unsigned long long>(midiOut));
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
	return 0;
}
