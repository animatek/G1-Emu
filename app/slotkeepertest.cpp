// g1slotkeepertest: the SlotKeeper against an emulated G1, with no DAW and no editor (issue #25).
//
//   1. A G1 boots with the factory flash; a keeper restores a small patch into slot A (the
//      sections NME uploads for SimpleOSC: OscA into the 2-Output) and leaves B-D empty.
//      Before the restore the G1 is silent, after it OscA drones (it needs no note).
//   2. A second keeper, as a project reopened later would have, starts on the same running G1
//      and asks for all four slots. Slot A must come back as the same patch (padding bits, the
//      header's last 3 bits and the cables' order aside) and the other three empty.
//   3. An editor uploads the patch into slot B through the keeper: it gets its ACKs, and the
//      keeper reads slot B afterwards.
//   4. Knob 1 is turned on the panel: the keeper reads slot A again (#46).
//   5. Bend Range is changed on the System menu: the keeper reads slot A again when asked (reread()).
//   6. pack()/unpack() keep the slots as they are.
//
// Exit code 77 (skipped) without a ROM, 1 on any failure.

#include "engine.h"
#include "hostconfig.h"
#include "romfinder.h"
#include "slotkeeper.h"
#include "g1Lib/g1knobs.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
	constexpr uint64_t g_ms = g1::g_ucClock / 1000;

	// NME's upload of SimpleOSC.pch, CustomDump left empty (it carries editor-only values).
	const std::vector<std::vector<uint8_t>> g_simpleOsc = {
		{0x37, 0x00, 0x00, 0x00, 0x53, 0x69, 0x6d, 0x70, 0x6c, 0x65, 0x4f, 0x53, 0x43, 0x00},
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

	struct Bench
	{
		g1app::Engine& engine;
		g1app::SlotKeeper* keeper = nullptr;
		double peak = 0;

		uint64_t nowMs() { return engine.mc().ucCycles() / g_ms; }

		// Runs the G1 for _ms, a millisecond at a time, with the keeper on its PC Port.
		void run(const uint64_t _ms)
		{
			auto& mc = engine.mc();
			std::vector<uint8_t> fromG1, toEditor, toG1;
			for(uint64_t i = 0; i < _ms; ++i)
			{
				const auto end = mc.ucCycles() + g_ms;
				while(mc.ucCycles() < end)
					mc.exec();
				fromG1.clear();
				mc.getPcPort().takeTx(fromG1);
				if(keeper)
				{
					toEditor.clear();
					keeper->g1Sent(fromG1, nowMs(), toEditor);
					toG1.clear();
					keeper->tick(nowMs(), toG1);
					if(!toG1.empty())
						mc.getPcPort().receive(toG1);
				}
			}
			mc.syncDsps();
		}

		bool runUntilSettled(const uint64_t _maxMs)
		{
			for(uint64_t t = 0; t < _maxMs; t += 100)
			{
				run(100);
				if(keeper->settled())
					return true;
			}
			return false;
		}
	};

	std::string bitsOf(const std::vector<uint8_t>& _b)
	{
		std::string bits;
		for(const auto c : _b)
			for(int i = 7; i >= 0; --i)
				bits += ((c >> i) & 1) ? '1' : '0';
		return bits;
	}

	// The same patch? Section by section in upload order: the last byte is padding, the header's
	// last 3 bits are the OS's own, and the OS may list cables in another order.
	bool samePatch(const std::vector<std::vector<uint8_t>>& _a, const std::vector<std::vector<uint8_t>>& _b, std::string& _why)
	{
		if(_a.size() != _b.size())
		{
			_why = "section count " + std::to_string(_a.size()) + " vs " + std::to_string(_b.size());
			return false;
		}
		for(size_t i = 0; i < _a.size(); ++i)
		{
			auto x = _a[i], y = _b[i];
			if(x.size() != y.size() || x.empty())
			{
				_why = "section " + std::to_string(i) + " size";
				return false;
			}
			if(x[0] == 33 && x.size() == 11)
			{
				x[10] &= 0xf8;
				y[10] &= 0xf8;
			}
			if(x[0] == 82)
			{
				const auto bx = bitsOf(x), by = bitsOf(y);
				const auto n = static_cast<size_t>(std::stoi(bx.substr(9, 15), nullptr, 2));
				std::vector<std::string> rx, ry;
				for(size_t c = 0; c < n; ++c)
				{
					rx.push_back(bx.substr(24 + 30 * c, 30));
					ry.push_back(by.substr(24 + 30 * c, 30));
				}
				std::sort(rx.begin(), rx.end());
				std::sort(ry.begin(), ry.end());
				if(rx != ry || bx.substr(0, 24) != by.substr(0, 24))
				{
					_why = "cables";
					return false;
				}
				continue;
			}
			if(!std::equal(x.begin(), x.end() - 1, y.begin()))
			{
				_why = "section " + std::to_string(i) + " (type " + std::to_string(x[0]) + ")";
				return false;
			}
		}
		return true;
	}
}

int main()
{
	g1app::HostOptions options;
	options.load(g1app::defaultSettingsPath());
	if(const char* v = std::getenv("G1_ROM"))
		options.rom = v;
	const auto search = g1app::findRom({}, options.rom);
	std::vector<uint8_t> rom;
	if(!search.found() || !g1app::inspectRom(search.path, rom).ok())
	{
		std::printf("skipped: no ROM\n");
		return 77;
	}

	int failures = 0;
	auto check = [&](const bool _ok, const std::string& _what)
	{
		std::printf("%s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++failures;
	};

	std::string osNote;
	g1app::Engine engine(rom, options.loadOs(osNote));
	if(!osNote.empty())
		std::printf("%s\n", osNote.c_str());
	// G1_FLASH=file: start from that flash (a standalone's flash.bin) instead of the factory's.
	if(const char* fl = std::getenv("G1_FLASH"))
	{
		std::vector<uint8_t> image;
		if(FILE* f = std::fopen(fl, "rb"))
		{
			int c;
			while((c = std::fgetc(f)) != EOF)
				image.push_back(static_cast<uint8_t>(c));
			std::fclose(f);
		}
		check(engine.loadFlash(image), std::string("flash from ") + fl);
	}
	Bench bench{engine};
	double peak = 0;
	engine.mc().getDsp(3).setBlockCallback([&](const int32_t _a, const int32_t _b, int32_t, int32_t)
	{
		peak = std::max({peak, std::abs(static_cast<double>(_a - g1app::Engine::OutputDc)), std::abs(static_cast<double>(_b - g1app::Engine::OutputDc))});
	});

	// 1. Restore SimpleOSC into slot A. Before it, the G1 is silent; after it, OscA drones.
	g1app::SlotKeeper first;
	g1app::SlotKeeper::Slots saved;
	saved[0].sections = g_simpleOsc;
	first.restore(saved);
	bench.keeper = &first;
	bench.run(2000);
	const double silent = peak;
	check(bench.runUntilSettled(20000), "the first keeper restores slot A and settles");
	std::string why;
	check(first.slots()[0].sections == g_simpleOsc, "it keeps slot A as restored");
	peak = 0;
	bench.run(500);
	std::printf("     peak before the restore %.0f, after it %.0f\n", silent, peak);
	check(silent < 10 && peak > 1000, "the restored patch sounds");

	// 2. A second keeper reads all four slots back from the G1.
	g1app::SlotKeeper second;
	bench.keeper = &second;
	check(bench.runUntilSettled(30000), "a second keeper reads the four slots and settles");
	const auto got = second.slots();
	check(samePatch(g_simpleOsc, got[0].sections, why), "slot A comes back as the same patch" + (why.empty() ? std::string() : " (" + why + ")"));
	check(got[1].empty() && got[2].empty() && got[3].empty(), "slots B-D come back empty");

	// 3. An editor uploads the same patch into slot B, packet by packet on each ACK, through the
	//    keeper as the runner passes it. The editor must get its ACKs (the keeper hides only its
	//    own), and the keeper must notice and read slot B afterwards.
	{
		auto& mc = engine.mc();
		const auto packets = g1app::SlotKeeper::uploadMessages(g_simpleOsc, 1);
		size_t sent = 0, acked = 0;
		std::vector<uint8_t> fromG1, toEditor, toG1;
		bool waiting = false;
		for(uint64_t t = 0; t < 15000 && !(sent == packets.size() && !waiting && second.settled()); ++t)
		{
			if(!waiting && sent < packets.size())
			{
				mc.getPcPort().receive(packets[sent]);
				second.editorSent(packets[sent], bench.nowMs());
				++sent;
				waiting = true;
			}
			const auto end = mc.ucCycles() + g_ms;
			while(mc.ucCycles() < end)
				mc.exec();
			fromG1.clear();
			mc.getPcPort().takeTx(fromG1);
			toEditor.clear();
			second.g1Sent(fromG1, bench.nowMs(), toEditor);
			for(size_t k = 0; k + 5 < toEditor.size(); ++k)
				if(toEditor[k] == 0xf0 && (toEditor[k + 2] >> 2) == 0x16 && (toEditor[k + 5] == 0x36 || toEditor[k + 5] == 0x7f))
				{
					++acked;
					waiting = false;
				}
			toG1.clear();
			second.tick(bench.nowMs(), toG1);
			if(!toG1.empty())
				mc.getPcPort().receive(toG1);
		}
		std::printf("     editor: %zu packets sent, %zu ACKs seen\n", sent, acked);
		check(sent == packets.size() && acked >= packets.size(), "an editor's upload gets its ACKs through the keeper");
		why.clear();
		check(samePatch(g_simpleOsc, second.slots()[1].sections, why), "the keeper reads slot B after the editor's upload" + (why.empty() ? std::string() : " (" + why + ")"));
	}

	// 4. A panel knob turned on slot A: the OS reports it as an Info message, not a Parameter,
	//    and the keeper must read slot A again so a project keeps the turn (issue #46).
	{
		auto& mc = engine.mc();
		g1::KnobMap knobs(mc);
		check(knobs.read(0).assigned, "knob 1 moves a parameter of slot A");
		const auto before = second.slots()[0].sections;
		const auto adc = g1::KnobMap::KnobAdc[0];
		for(int p = 0; p < 100; ++p)
		{
			mc.setAdc(adc, static_cast<uint8_t>(20 + p));
			bench.run(4);
		}
		check(bench.runUntilSettled(10000), "the keeper settles after the turn");
		std::printf("     knob 1 now gives %d\n", knobs.read(0).value);
		check(second.slots()[0].sections != before, "the keeper reads slot A again after a panel knob turn");
	}

	// 5. Bend Range changed on the System menu's Patch side: the OS tells no one, so the keeper
	//    keeps slot A as it was; asked to read it again (reread(), as the plugin does after the
	//    menu), it has the new header (#46).
	{
		auto& mc = engine.mc();
		auto press = [&](const uint32_t _row, const uint32_t _bit)
		{
			mc.setButton(_row, _bit, true);
			bench.run(150);
			mc.setButton(_row, _bit, false);
			bench.run(300);
		};
		bench.keeper = &second;
		const auto before = second.slots()[0].sections;
		press(0, 7);	// System
		press(1, 7);	// Right: PATCH
		for(int i = 0; i < 9 && mc.getLcd().line(0, 16).find("BEND RANGE") == std::string::npos; ++i)
			press(1, 6);	// Down
		check(mc.getLcd().line(0, 16).find("BEND RANGE") != std::string::npos, "the System menu shows BEND RANGE");
		mc.turnDial(5);
		bench.run(500);
		std::printf("     display [%s|%s]\n", mc.getLcd().line(0, 16).c_str(), mc.getLcd().line(1, 16).c_str());
		press(1, 3);	// Patch/Load: out of the menu
		bench.runUntilSettled(10000);
		check(second.slots()[0].sections == before, "the OS tells no one: the keeper still has slot A as it was");
		second.reread(0);
		check(bench.runUntilSettled(10000), "asked to read slot A again, the keeper settles");
		const auto after = second.slots()[0].sections;
		check(after.size() > 1 && before.size() > 1 && after[1] != before[1], "slot A's header has the new Bend Range");
	}

	// 6. Project bytes.
	g1app::SlotKeeper::Slots back;
	check(g1app::SlotKeeper::unpack(g1app::SlotKeeper::pack(got), back) && back == got, "pack/unpack keep the slots");
	check(!g1app::SlotKeeper::unpack({1, 2, 3}, back), "unpack refuses what is not its format");

	std::printf("%s\n", failures ? "FAILED" : "passed");
	return failures ? 1 : 0;
}
