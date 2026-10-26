// g1flashkeeptest: a saved flash comes back as it was (2026-10-04).
//
// A G1 boots on an empty flash, which its OS formats (INIT FLASH), and the flash is kept, as the
// standalone keeps flash.bin. A second G1 loads it, as the standalone does at its next start, and
// boots: it must not format or erase anything, and the OS's own marks in the first bytes must
// still be there. Engine::loadFlash once put the whole OS area back from the factory flash, which
// erased the "formatted" mark at +0 and made every start format the flash, banks and all.
// With the OS image of the settings or G1_OS if there is one, else the ROM's. Exit code 77
// (skipped) without a ROM, 1 on failure.
//
// With an OS image, also a plugin's state across OSes (issue #25): made under the ROM's OS and
// restored under the image, and the other way round. The banks and settings must come back as
// they were, unformatted; setUserState once refused it and the plugin started empty.

#include "engine.h"
#include "hostconfig.h"
#include "romfinder.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <string>

namespace
{
	constexpr uint64_t g_ms = g1::g_ucClock / 1000;

	void run(g1app::Engine& _e, const uint64_t _ms)
	{
		auto& mc = _e.mc();
		const auto end = mc.ucCycles() + _ms * g_ms;
		std::vector<uint8_t> drop;
		while(mc.ucCycles() < end)
		{
			mc.exec();
			drop.clear();
			mc.getPcPort().takeTx(drop);
		}
		mc.syncDsps();
	}
}

int main()
{
	g1app::HostOptions options;
	options.load(g1app::defaultSettingsPath());
	// The settings give the ROM, not the model: one left on the Micro would hand a 4-DSP G1 the Micro's
	// OS, which then waits for ever (filling memory with its log). G1_MODEL still picks one.
	options.expanded = options.micro = false;
	if(const char* v = std::getenv("G1_ROM"))
		options.rom = v;
	const auto search = g1app::findRom({}, options.rom);
	std::vector<uint8_t> rom;
	if(!search.found() || !g1app::inspectRom(search.path, rom).ok())
	{
		std::printf("skipped: no ROM\n");
		return 77;
	}
	std::string osNote;
	const auto os = options.loadOs(osNote);
	std::printf("%s\n", osNote.empty() ? "OS: the ROM's" : osNote.c_str());

	std::vector<uint8_t> kept;
	{
		g1app::Engine first(rom, os);
		run(first, 8000);
		auto& f = first.mc().getFlash();
		std::printf("first boot: %u sectors erased, %u bytes programmed (formatting)\n", f.erasedSectors(), f.programmedBytes());
		kept = f.data();
	}
	g1app::Engine second(rom, os);
	const bool loaded = second.loadFlash(kept);
	run(second, 8000);
	auto& f = second.mc().getFlash();
	std::printf("second boot on the kept flash: %u sectors erased, %u bytes programmed, mark at +0 %02x%02x%02x%02x\n",
		f.erasedSectors(), f.programmedBytes(), f.data()[0], f.data()[1], f.data()[2], f.data()[3]);
	bool ok = loaded && f.erasedSectors() == 0 && f.data()[0] == kept[0] && f.data()[3] == kept[3] && kept[3] != 0xff;

	if(os.empty())
		std::printf("state across OSes: skipped, no OS image\n");
	for(const bool toImage : {true, false})
	{
		if(os.empty())
			break;
		g1app::Engine from(rom, toImage ? std::vector<uint8_t>{} : os);
		from.loadFlash(kept);
		run(from, 8000);
		const auto state = from.userState();
		g1app::Engine to(rom, toImage ? os : std::vector<uint8_t>{});
		std::string error;
		bool otherOs = false;
		const bool restored = to.setUserState(state, error, &otherOs);
		run(to, 8000);
		const auto& a = from.mc().getFlash().data();
		auto& t = to.mc().getFlash();
		const bool same = std::equal(a.begin() + g1app::Engine::OsBytes, a.end(), t.data().begin() + g1app::Engine::OsBytes);
		const bool pass = restored && otherOs && t.erasedSectors() == 0 && same && t.data()[0] == a[0] && t.data()[3] == a[3];
		std::printf("state from the %s restored under the %s: %s%s, %u sectors erased, patch storage %s: %s\n",
			toImage ? "ROM's OS" : "OS image", toImage ? "OS image" : "ROM's OS", restored ? "taken" : "refused, ", error.c_str(),
			t.erasedSectors(), same ? "the same" : "DIFFERENT", pass ? "ok" : "FAILED");
		ok = ok && pass;
	}
	std::printf("%s\n", ok ? "passed" : "FAILED");
	return ok ? 0 : 1;
}
