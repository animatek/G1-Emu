// g1run: the emulated Nord Modular G1, in real time, in the console.
//
//   g1run [ROM] [FLASH]
//
// ROM   = the 512 KB rack ROM (OS 3.03). Never goes into the repo. Without it, the ROM named in
//         the settings file is used, and failing that the ROM folders are searched (romfinder.h);
//         when none of them has one, the message says where to put it.
// FLASH = where the 1 MB flash is saved (installed OS + stored patches). Default
//         ~/.local/share/Animatek/G1-Emu/flash.bin. If missing, it is created with the
//         factory OS from the ROM, like a freshly updated G1.
//
// Creates the ALSA client "G1-Emu" with two ports, like the hardware:
//   "PC Port" = the editor's PC PORT (NME connects here)
//   "MIDI"    = the regular MIDI IN/OUT
// Audio goes through JACK (PipeWire) if there is a server: client "G1-Emu" with out_1..out_4 and
// in_L/in_R, like the back panel; out_1/out_2 connect themselves to the card (not with G1_JACK_CONNECT=0).
// Without JACK, or with G1_AUDIO=alsa or G1_AUDIO=device, outputs 1/2 go through ALSA.
// G1_AUDIO=no disables it. G1_GAIN_DB raises the level (default +36 dB: undoes the -36 dB
// cap the OS puts on the master volume, see NOTES.md).
// Ctrl+C saves the flash and quits. All the work is done by EmuHost (emuhost.h), which the
// window (g1gui) also uses.

#include "emuhost.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <thread>

namespace
{
	std::atomic<bool> g_quit{false};
}

int main(int argc, char** argv)
{
	g1app::EmuHost host;
	std::string log;
	const bool ok = host.start(argc > 1 ? argv[1] : "", argc > 2 ? argv[2] : "", log);
	std::printf("%s", log.c_str());
	std::fflush(stdout);
	if(!ok)
		return 1;

	std::signal(SIGINT, [](int) { g_quit = true; });
	std::signal(SIGTERM, [](int) { g_quit = true; });

	auto last = std::chrono::steady_clock::now();
	while(!g_quit)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		const auto now = std::chrono::steady_clock::now();
		if(now - last >= std::chrono::seconds(5))
		{
			last = now;
			std::printf("%s\n", host.report().c_str());
			std::fflush(stdout);
		}
	}
	host.stop();
	std::printf("\nflash saved\n");
	return 0;
}
