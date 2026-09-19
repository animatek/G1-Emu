// g1run: el Nord Modular G1 emulado, en tiempo real, en la consola.
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
// El audio va por JACK (PipeWire) si hay servidor: cliente "G1-Emu" con out_1..out_4 e in_L/in_R,
// como el panel trasero; out_1/out_2 se conectan solos a la tarjeta (G1_JACK_CONNECT=0 no).
// Si no hay JACK, o con G1_AUDIO=alsa o G1_AUDIO=dispositivo, salidas 1/2 por ALSA.
// G1_AUDIO=no lo desactiva. G1_GAIN_DB sube el nivel (por defecto +36 dB: deshace el tope
// de -36 dB que el OS pone al volumen maestro, ver NOTAS.md).
// Ctrl+C guarda la flash y sale. Todo el trabajo lo hace EmuHost (emuhost.h), que tambien usa
// la ventana (g1gui).

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
	if(argc < 2)
	{
		std::fprintf(stderr, "uso: g1run ROM [FLASH]\n");
		return 2;
	}
	g1app::EmuHost host;
	std::string log;
	const bool ok = host.start(argv[1], argc > 2 ? argv[2] : "", log);
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
	std::printf("\nflash guardada\n");
	return 0;
}
