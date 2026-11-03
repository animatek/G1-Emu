// g1sharedbankstest: the G1's memory shared by every front end (2026-11-03).
//
// In a scratch file: a first G1 makes the memory. Two G1s, A and B, start from it, as two plugin
// instances do. A stores SimpleOSC into bank 1, position 1, and syncs: written, since the file still
// holds what A read. B then stores another patch ("SecondOSC") into position 2 and syncs: behind,
// since A wrote since, so nothing of B's is written over A's; B's store is replayed into the file by
// a G1 of its own. A new G1 started from the file must have both patches, and the OS's storage must
// boot without being formatted again.
// With G1_MODEL=micro the same on the Micro. Exit code 77 (skipped) without a ROM, 1 on failure.

#include "engine.h"
#include "hostconfig.h"
#include "osimage.h"
#include "romfinder.h"
#include "sharedbanks.h"
#include "slotkeeper.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

namespace
{
	constexpr uint64_t g_ms = g1::g_ucClock / 1000;

	std::vector<std::vector<uint8_t>> patchNamed(const char* _name)
	{
		std::vector<std::vector<uint8_t>> p = {
			{0x37, 0x00, 0x00, 0x00},
			{0x21, 0x01, 0xfc, 0x07, 0xf1, 0x00, 0x40, 0x7d, 0x02, 0xfe, 0x78},
			{0x4a, 0x82, 0x0e, 0x04, 0x10, 0x30, 0x80, 0x81, 0x09},
			{0x4a, 0x00},
			{0x69, 0x80, 0x00, 0x00, 0x20, 0x00, 0x00},
			{0x52, 0x80, 0x02, 0x00, 0x40, 0x82, 0x00, 0x01, 0x02, 0x08, 0x10},
			{0x52, 0x00, 0x00},
			{0x4d, 0x82, 0x02, 0x1e, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x02, 0x09, 0x90, 0x00},
			{0x4d, 0x00},
			{0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
			{0x62, 0xc0, 0x40, 0x60, 0x20, 0x40, 0x00, 0x00},
			{0x60, 0x00},
			{0x5b, 0x80},
			{0x5b, 0x00},
			{0x5a, 0x82, 0x01, 0x4f, 0x73, 0x63, 0x41, 0x00, 0x02, 0x32, 0x4f, 0x75, 0x74, 0x70, 0x75, 0x74, 0x00},
			{0x5a, 0x00}};
		for(const char* c = _name; *c; ++c)
			p[0].push_back(static_cast<uint8_t>(*c));
		p[0].push_back(0);
		return p;
	}

	std::vector<uint8_t> frame(const uint8_t _cc, const uint8_t _slot, const std::vector<uint8_t>& _payload)
	{
		std::vector<uint8_t> m = {0xf0, 0x33, static_cast<uint8_t>(((_cc & 0x1f) << 2) | (_slot & 3)), 0x06};
		m.insert(m.end(), _payload.begin(), _payload.end());
		uint32_t sum = 0;
		for(const auto b : m)
			sum += b;
		m.push_back(static_cast<uint8_t>(sum & 0x7f));
		m.push_back(0xf7);
		return m;
	}

	void run(g1app::Engine& _e, const uint64_t _ms, std::vector<uint8_t>* _seen = nullptr)
	{
		auto& mc = _e.mc();
		std::vector<uint8_t> rx;
		for(uint64_t i = 0; i < _ms; ++i)
		{
			const auto end = mc.ucCycles() + g_ms;
			while(mc.ucCycles() < end)
				mc.exec();
			rx.clear();
			mc.getPcPort().takeTx(rx);
			if(_seen)
				_seen->insert(_seen->end(), rx.begin(), rx.end());
		}
		mc.syncDsps();
	}

	bool acked(const std::vector<uint8_t>& _b)
	{
		for(size_t k = 0; k + 5 < _b.size(); ++k)
			if(_b[k] == 0xf0 && _b[k + 1] == 0x33 && (_b[k + 2] >> 2) == 0x16)
				return true;
		return false;
	}

	// Upload into slot A and store into bank 1, _position, as an editor does.
	void store(g1app::Engine& _e, const std::vector<std::vector<uint8_t>>& _patch, const uint8_t _position)
	{
		auto& mc = _e.mc();
		for(const auto& p : g1app::SlotKeeper::uploadMessages(_patch, 0))
		{
			mc.getPcPort().receive(p);
			std::vector<uint8_t> seen;
			for(int t = 0; t < 4000 && !acked(seen); t += 10)
				run(_e, 10, &seen);
		}
		mc.getPcPort().receive(frame(0x17, 0, {0x41, 0x0b, 0x00, 0x00, _position}));
		run(_e, 1500);
	}

	std::string bank1(g1app::Engine& _e)
	{
		_e.mc().getPcPort().receive(frame(0x17, 0, {0x41, 0x14, 0x00, 0x00}));
		std::vector<uint8_t> seen;
		run(_e, 500, &seen);
		return {seen.begin(), seen.end()};
	}

	const char* syncName(const g1app::SharedBanks::Sync _s)
	{
		switch(_s)
		{
		case g1app::SharedBanks::Sync::Written: return "written";
		case g1app::SharedBanks::Sync::Unchanged: return "unchanged";
		case g1app::SharedBanks::Sync::Behind: return "behind";
		default: return "failed";
		}
	}
}

int main()
{
	g1app::HostOptions options;
	options.load(g1app::defaultSettingsPath());
	const char* model = std::getenv("G1_MODEL");
	const bool micro = options.micro = model && std::string(model) == "micro";
	options.expanded = false;
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
	if(micro && os.empty())
	{
		std::printf("skipped: no Micro OS\n");
		return 77;
	}
	const uint32_t dsps = micro ? g1::g_microDsps : g1::g_regularDsps;

	int failures = 0;
	auto check = [&](const bool _ok, const std::string& _what)
	{
		std::printf("%s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		failures += _ok ? 0 : 1;
	};

	// Its own directory per run: the rack and the Micro run at once under ctest.
	const auto dir = std::filesystem::temp_directory_path() / ("g1sharedbankstest-" + std::string(micro ? "micro-" : "rack-") + std::to_string(std::random_device{}()));
	std::filesystem::create_directories(dir);
	const auto path = (dir / "flash.bin").string();
	std::string note;

	// The memory, made by a first G1 (formatted at its first boot).
	{
		g1app::Engine first(rom, os, dsps);
		g1app::SharedBanks shared;
		shared.setPath(path);
		check(!shared.loadInto(first), "no shared memory yet");
		run(first, 8000);
		const auto s = shared.sync(first.mc().getFlash().data(), true, note);
		check(s == g1app::SharedBanks::Sync::Written, std::string("the first G1 makes it: ") + syncName(s));
	}

	g1app::Engine a(rom, os, dsps), b(rom, os, dsps);
	g1app::SharedBanks sa, sb;
	sa.setPath(path);
	sb.setPath(path);
	check(sa.loadInto(a) && sb.loadInto(b), "A and B start from it");
	run(a, 8000);
	run(b, 8000);
	check(a.mc().getFlash().erasedSectors() == 0 && b.mc().getFlash().erasedSectors() == 0, "neither formats it again");

	const auto first = patchNamed("SimpleOSC"), second = patchNamed("SecondOSC");
	store(a, first, 0);
	auto s = sa.sync(a.mc().getFlash().data(), false, note);
	check(s == g1app::SharedBanks::Sync::Written, std::string("A stores into 1:1 and syncs: ") + syncName(s));

	store(b, second, 1);
	s = sb.sync(b.mc().getFlash().data(), false, note);
	check(s == g1app::SharedBanks::Sync::Behind, std::string("B stores into 1:2 and syncs: ") + syncName(s) + " (A wrote since)");
	g1app::SharedBanks::Store record;
	record.bank = 0;
	record.position = 1;
	record.sections = second;
	const bool replayed = sb.replay(rom, os, dsps, {record}, note);
	std::printf("     replay: %s\n", note.c_str());
	check(replayed, "B's store is replayed into the shared memory");

	g1app::Engine c(rom, os, dsps);
	g1app::SharedBanks sc;
	sc.setPath(path);
	check(sc.loadInto(c), "a new G1 starts from it");
	run(c, 8000);
	check(c.mc().getFlash().erasedSectors() == 0, "it boots without formatting");
	const auto list = bank1(c);
	check(list.find("SimpleOSC") != std::string::npos, "bank 1 has A's SimpleOSC");
	check(list.find("SecondOSC") != std::string::npos, "bank 1 has B's SecondOSC");

	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	std::printf("%s\n", failures ? "FAILED" : "passed");
	return failures ? 1 : 0;
}
