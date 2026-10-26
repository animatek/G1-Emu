// g1presetstest: the PresetsLink, the synth's banks for the Presets page.
//
//   1. decodeList on a hand-made answer: names, an empty position skipped, the end of the list.
//   2. Against an emulated G1, with a flash that has patches (G1_FLASH, or the standalone's
//      flash.bin): bank 1 is read, and loading its first patch into slot A puts that patch's name
//      on the G1's display, as a load from the panel would.
//   3. A MIDI controller for what knob 1 moves, as the knob menu gives it: assigned, moving the
//      parameter on slot A's channel, and taken away again; the editor is told each time.
//
// Exit code 77 (skipped) without a ROM or a flash with patches for part 2, 1 on any failure.

#include "engine.h"
#include "hostconfig.h"
#include "presetslink.h"
#include "g1Lib/g1knobs.h"
#include "romfinder.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace
{
	constexpr uint64_t g_ms = g1::g_ucClock / 1000;

	std::string trim(std::string _s)
	{
		while(!_s.empty() && _s.back() == ' ')
			_s.pop_back();
		return _s;
	}
}

int main()
{
	int failures = 0;
	auto check = [&](const bool _ok, const std::string& _what)
	{
		std::printf("%s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++failures;
	};

	// 1. A hand-made answer: three unknown bytes, "Abc" at 5, an empty position, "D" at 7, the end.
	{
		const std::vector<uint8_t> content = {6, 22, 1, 'A', 'b', 'c', 0, 2, 'D', 0, 4};
		int nb = 0, np = 0;
		const auto e = g1app::PresetsLink::decodeList(content, 0, 5, nb, np);
		check(e.size() == 2 && e[0].position == 5 && e[0].name == "Abc" && e[1].position == 7 && e[1].name == "D" && nb == -1,
			"decodeList: names, an empty position, the end");
		// Sixteen characters have no zero after them.
		const std::vector<uint8_t> full = {6, 22, 1, 'S','i','x','t','e','e','n',' ','c','h','a','r','s','!','!','!', 'X', 0, 0};
		const auto f = g1app::PresetsLink::decodeList(full, 2, 0, nb, np);
		check(f.size() == 2 && f[0].name == "Sixteen chars!!!" && f[1].name == "X" && f[1].position == 1 && nb == 2 && np == 2,
			"decodeList: a 16-character name, then the next, and where to go on");
		// A ControlMap: type 96, then 2 assignments of 7 + 2 + 7 + 7 bits: CC 20 on poly module 3
		// parameter 1, CC 7 on common module 2 parameter 0.
		std::vector<uint8_t> map = {96};
		uint32_t acc = 0;
		int held = 0;
		const auto put = [&](const int _v, const int _bits)
		{
			for(int b = _bits - 1; b >= 0; --b)
			{
				acc = (acc << 1) | ((_v >> b) & 1);
				if(++held == 8)
				{
					map.push_back(static_cast<uint8_t>(acc));
					acc = 0;
					held = 0;
				}
			}
		};
		put(2, 7);
		put(20, 7); put(1, 2); put(3, 7); put(1, 7);
		put(7, 7); put(0, 2); put(2, 7); put(0, 7);
		if(held)
			map.push_back(static_cast<uint8_t>(acc << (8 - held)));
		const auto c = g1app::PresetsLink::decodeControls(map);
		check(c.size() == 2 && c[0].ctrl == 20 && c[0].section == 1 && c[0].module == 3 && c[0].param == 1
			&& c[1].ctrl == 7 && c[1].section == 0 && c[1].module == 2 && c[1].param == 0, "decodeControls: two MIDI controllers");
	}

	g1app::HostOptions options;
	options.load(g1app::defaultSettingsPath());
	// The settings give the ROM, not the model: one left on the Micro would hand a 4-DSP G1 the Micro's
	// OS, which then waits for ever (filling memory with its log). G1_MODEL still picks one.
	options.expanded = options.micro = false;
	if(const char* v = std::getenv("G1_ROM"))
		options.rom = v;
	const auto search = g1app::findRom({}, options.rom);
	std::vector<uint8_t> rom;
	const char* flashPath = std::getenv("G1_FLASH");
	const std::string flashFile = flashPath ? flashPath : g1app::defaultFlashPath();
	std::ifstream ff(flashFile, std::ios::binary);
	const std::vector<uint8_t> flash((std::istreambuf_iterator<char>(ff)), std::istreambuf_iterator<char>());
	if(!search.found() || !g1app::inspectRom(search.path, rom).ok() || flash.empty())
	{
		std::printf("%s; part 2 skipped: no ROM or no flash with patches\n", failures ? "FAILED" : "part 1 ok");
		return failures ? 1 : 77;
	}

	std::string osNote;
	g1app::Engine engine(rom, options.loadOs(osNote));
	check(engine.loadFlash(flash), "flash loaded from " + flashFile);
	auto& mc = engine.mc();
	g1app::PresetsLink link;
	std::vector<uint8_t> fromG1, toEditor, toG1, seen;	// seen: all that reached the editor
	std::vector<uint8_t> sent;							// all the G1 sent, kept or not
	auto run = [&](const uint64_t _ms)
	{
		for(uint64_t i = 0; i < _ms; ++i)
		{
			const auto end = mc.ucCycles() + g_ms;
			while(mc.ucCycles() < end)
				mc.exec();
			const auto now = mc.ucCycles() / g_ms;
			fromG1.clear();
			mc.getPcPort().takeTx(fromG1);
			sent.insert(sent.end(), fromG1.begin(), fromG1.end());
			toEditor.clear();
			link.g1Sent(fromG1, now, toEditor);
			seen.insert(seen.end(), toEditor.begin(), toEditor.end());
			toG1.clear();
			link.tick(now, toG1);
			if(!toG1.empty())
				mc.getPcPort().receive(toG1);
		}
	};

	// The OS answers an editor that has greeted it, as NME does first.
	run(2000);
	mc.getPcPort().receive({0xf0, 0x33, 0x00, 0x06, 0x00, 0x03, 0x03, 0xf7});
	run(500);

	link.readBank(0);
	g1app::PresetsLink::BankNames names;
	bool known = false;
	for(int t = 0; t < 200 && !known; ++t)
	{
		run(50);
		known = link.bank(0, names);
	}
	int used = 0, first = -1;
	for(int p = 0; p < g1app::PresetsLink::Positions; ++p)
		if(!names[static_cast<size_t>(p)].empty())
		{
			++used;
			if(first < 0)
				first = p;
		}
	check(known, "bank 1 read: " + std::to_string(used) + " of 99 positions used");
	if(first < 0)
	{
		std::printf("bank 1 is empty: the load is not checked\n");
		return failures ? 1 : 77;
	}
	for(int p = 0, shown = 0; p < g1app::PresetsLink::Positions && shown < 6; ++p)
		if(!names[static_cast<size_t>(p)].empty())
		{
			std::printf("     %02d %s\n", p + 1, names[static_cast<size_t>(p)].c_str());
			++shown;
		}

	// An editor greets the synth every 300 ms, as Animatek NME's checks do (IAm), through the
	// load: the load must not wait for a silence that never comes.
	seen.clear();
	const auto greet = [&](const uint64_t _ms)
	{
		const std::vector<uint8_t> iam = {0xf0, 0x33, 0x00, 0x06, 0x00, 0x03, 0x03, 0xf7};
		for(uint64_t t = 0; t < _ms; t += 50)
		{
			if(t % 300 == 0)
			{
				mc.getPcPort().receive(iam);
				link.editorSent(iam, mc.ucCycles() / g_ms);
			}
			run(50);
		}
	};
	greet(600);
	const auto asked = mc.ucCycles() / g_ms;
	link.load(0, 0, first);
	std::string shown;
	uint64_t toldAt = 0;
	size_t askedFrom = 0, sentFrom = 0;	// where in seen and in sent the editor's own request was made
	for(int t = 0; t < 60; ++t)
	{
		greet(50);
		for(size_t i = 0; toldAt == 0 && i + 7 < seen.size(); ++i)
			if(seen[i] == 0xf0 && (seen[i + 2] >> 2) == 0x14 && seen[i + 5] == 0x38)
			{
				toldAt = mc.ucCycles() / g_ms;
				// The editor, told of the new patch, asks for it at once (RequestPatch, $41 $35, as
				// Animatek NME does): the OS's answer must reach it, not be kept as the link's.
				std::vector<uint8_t> req = {0xf0, 0x33, 0x5c, 0x06, 0x41, 0x35, 0x00, 0xf7};
				uint32_t sum = 0;
				for(size_t k = 0; k + 2 < req.size(); ++k)
					sum += req[k];
				req[req.size() - 2] = static_cast<uint8_t>(sum & 0x7f);
				mc.getPcPort().receive(req);
				link.editorSent(req, mc.ucCycles() / g_ms);
				askedFrom = seen.size();
				sentFrom = sent.size();
			}
		shown = trim(mc.getLcd().line(0));
		if(toldAt && shown.find(trim(names[static_cast<size_t>(first)])) != std::string::npos)
			break;
	}
	check(toldAt && toldAt - asked < 600, "with an editor greeting every 300 ms, the editor hears of the load "
		+ std::to_string(toldAt ? toldAt - asked : 0) + " ms after it is asked for");
	check(shown.find(trim(names[static_cast<size_t>(first)])) != std::string::npos,
		"loaded position " + std::to_string(first + 1) + " into slot A: the display says \"" + shown + "\"");

	// Every ACK the G1 sent after the editor's request reaches the editor: the link keeps only the
	// answers to its own requests (a list, a load, an upload), never one to the editor's.
	const auto acks = [](const std::vector<uint8_t>& _b, const size_t _from)
	{
		std::vector<std::vector<uint8_t>> out;
		for(size_t i = _from; i + 5 < _b.size(); ++i)
			if(_b[i] == 0xf0 && _b[i + 1] == 0x33 && (_b[i + 2] >> 2) == 0x16)
			{
				const auto end = std::find(_b.begin() + static_cast<long>(i), _b.end(), uint8_t(0xf7));
				out.emplace_back(_b.begin() + static_cast<long>(i), end == _b.end() ? end : end + 1);
			}
		return out;
	};
	size_t lost = 0, editorsAcks = 0;
	for(const auto& a : acks(sent, sentFrom))
	{
		if(a[5] == 0x38 || a[5] == 0x13 || a[5] == 0x15)	// the link's own kinds
			continue;
		++editorsAcks;
		const auto got = acks(seen, askedFrom);
		lost += std::find(got.begin(), got.end(), a) == got.end() ? 1 : 0;
	}
	check(toldAt && lost == 0, "after the load, the editor's own answers reach it: " + std::to_string(editorsAcks)
		+ " ACK(s) for it, " + std::to_string(lost) + " kept from it");

	// The editor is told, as after a load from the panel: NewPatchInSlot (cc $14, sc $38) for slot A.
	bool told = false;
	for(size_t i = 0; i + 7 < seen.size(); ++i)
		told = told || (seen[i] == 0xf0 && seen[i + 1] == 0x33 && (seen[i + 2] >> 2) == 0x14 && seen[i + 5] == 0x38 && seen[i + 6] == 0);
	check(told, "the editor is told of the new patch in slot A (NewPatchInSlot)");

	// 3. A MIDI controller for what knob 1 moves in slot A, from the panel's knob menu: read what
	//    the patch has, assign a free CC, see it there, move the parameter with it on slot A's
	//    channel (1), and take it away again. The editor is told of the change each time.
	g1::KnobMap knobs(mc);
	const auto knob = knobs.read(0);
	if(!knob.assigned)
		std::printf("knob 1 moves nothing in this patch: the MIDI controllers are not checked\n");
	else
	{
		auto readNow = [&](std::vector<g1app::PresetsLink::Control>& _list, const uint64_t _after)
		{
			uint64_t rev = 0;
			for(int t = 0; t < 100; ++t)
			{
				greet(50);
				if(link.controls(0, _list, rev) && rev > _after)
					return rev;
			}
			return uint64_t(0);
		};
		const auto has = [&](const std::vector<g1app::PresetsLink::Control>& _list, const int _ctrl)
		{
			return std::any_of(_list.begin(), _list.end(), [&](const g1app::PresetsLink::Control& _c)
				{ return _c.ctrl == _ctrl && _c.section == knob.section && _c.module == knob.module && _c.param == knob.param; });
		};
		std::vector<g1app::PresetsLink::Control> list;
		link.readControls(0);
		const auto r0 = readNow(list, 0);
		check(r0 > 0, "slot A's MIDI controllers read: " + std::to_string(list.size()));
		int previous = -1, free = -1;
		for(const auto& c : list)
			if(c.section == knob.section && c.module == knob.module && c.param == knob.param)
				previous = c.ctrl;
		for(int cc = 20; cc < 120 && free < 0; ++cc)
			if(cc != 32 && cc != 64 && std::none_of(list.begin(), list.end(), [&](const g1app::PresetsLink::Control& _c) { return _c.ctrl == cc; }))
				free = cc;

		seen.clear();
		link.assignControl(0, knob.section, knob.module, knob.param, free, previous);
		const auto r1 = readNow(list, r0);
		check(r1 > r0 && has(list, free), "CC " + std::to_string(free) + " given to knob 1's " + knob.moduleName + " " + knob.paramName);
		greet(300);
		bool toldOfIt = false;
		for(size_t i = 0; i + 7 < seen.size(); ++i)
			toldOfIt = toldOfIt || (seen[i] == 0xf0 && (seen[i + 2] >> 2) == 0x14 && seen[i + 5] == 0x38 && seen[i + 6] == 0);
		check(toldOfIt, "the editor is told slot A's patch changed");

		// The CC moves the parameter, as an external controller would: low, then high.
		const auto value = [&] { return static_cast<int>(knobs.read(0).value); };
		mc.getSci().write(std::vector<uint8_t>{0xb0, static_cast<uint8_t>(free), 0});
		greet(200);
		const int low = value();
		mc.getSci().write(std::vector<uint8_t>{0xb0, static_cast<uint8_t>(free), 127});
		greet(200);
		const int high = value();
		check(high > low, "CC " + std::to_string(free) + " on channel 1 moves it: " + std::to_string(low) + " at 0, " + std::to_string(high) + " at 127");

		link.assignControl(0, knob.section, knob.module, knob.param, -1, free);
		const auto r2 = readNow(list, r1);
		check(r2 > r1 && !has(list, free), "CC " + std::to_string(free) + " taken away again: " + std::to_string(list.size()) + " left");
		if(previous >= 0)
			link.assignControl(0, knob.section, knob.module, knob.param, previous, -1);	// as it was
	}

	std::printf(failures ? "%d failure(s)\n" : "all ok\n", failures);
	return failures ? 1 : 0;
}
