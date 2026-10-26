// g1uploadgaptest (#81): an editor's patch upload with a long wait between its packets, as slow
// ACKs make it, while the synth settings' link wants to read (it re-reads every so often). The
// runner keeps the link out of an upload (SlotKeeper::editorUploading); without that, the link's
// request lands between two packets and the OS drops the upload ("Unfinished bubble", code 6).
//
//   1. Gated, as the runner does: both packets go in and the last one is taken.
//   2. Not gated, for the record: the same upload, to see whether the OS drops it.
//
// Exit code 77 (skipped) without a ROM, 1 on any failure.

#include "engine.h"
#include "hostconfig.h"
#include "pcsysex.h"
#include "romfinder.h"
#include "slotkeeper.h"
#include "synthsettings.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
	constexpr uint64_t g_ms = g1::g_ucClock / 1000;
	constexpr uint64_t g_gapMs = 1500;	// the editor waits this long after each ACK

	// NME's upload of SimpleOSC.pch, as in g1slotkeepertest.
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

	struct Outcome
	{
		size_t sent = 0, acked = 0;
		bool lastTaken = false;
		size_t linkSpoke = 0;	// the link's requests that went in during the upload
	};

	Outcome upload(const std::vector<uint8_t>& _rom, const std::vector<uint8_t>& _os, const bool _gated)
	{
		g1app::Engine engine(_rom, _os);
		auto& mc = engine.mc();
		g1app::SlotKeeper keeper;
		g1app::SynthSettingsLink link;
		link.setReread(200);
		const auto packets = g1app::SlotKeeper::uploadMessages(g_simpleOsc, 1);
		Outcome out;
		std::vector<uint8_t> fromG1, toEditor, toG1;
		const auto now = [&] { return mc.ucCycles() / g_ms; };
		bool waiting = false;
		uint64_t sendAt = 6000;	// the OS is up, and the link has read once
		bool uploading = false;
		for(uint64_t t = 0; t < 120000 && !out.lastTaken; ++t)
		{
			const auto end = mc.ucCycles() + g_ms;
			while(mc.ucCycles() < end)
				mc.exec();
			if(!waiting && out.sent < packets.size() && now() >= sendAt)
			{
				mc.getPcPort().receive(packets[out.sent]);
				keeper.editorSent(packets[out.sent], now());
				link.editorSent(packets[out.sent], now());
				++out.sent;
				waiting = true;
				uploading = true;
			}
			fromG1.clear();
			mc.getPcPort().takeTx(fromG1);
			toEditor.clear();
			keeper.g1Sent(fromG1, now(), toEditor);
			link.g1Sent(fromG1, now(), toEditor);
			for(size_t k = 0; k + 5 < toEditor.size(); ++k)
				if(toEditor[k] == 0xf0 && (toEditor[k + 2] >> 2) == 0x16 && (toEditor[k + 5] == 0x36 || toEditor[k + 5] == 0x7f))
				{
					++out.acked;
					waiting = false;
					sendAt = now() + g_gapMs;
					out.lastTaken |= toEditor[k + 5] == 0x7f;
				}
			// The links speak when the editor leaves them room, the settings' one not during an
			// editor's upload when gated, as the runner does.
			toG1.clear();
			keeper.tick(now(), toG1);
			if(!toG1.empty())
			{
				mc.getPcPort().receive(toG1);
				link.editorSent(toG1, now());
			}
			if(!_gated || !keeper.editorUploading(now()))
			{
				toG1.clear();
				link.tick(now(), toG1);
				if(!toG1.empty())
				{
					if(uploading && out.sent < packets.size())
						++out.linkSpoke;
					mc.getPcPort().receive(toG1);
					keeper.editorSent(toG1, now());
				}
			}
		}
		return out;
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
	int failures = 0;
	auto check = [&](const bool _ok, const std::string& _what)
	{
		std::printf("%s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++failures;
	};
	std::string osNote;
	const auto os = options.loadOs(osNote);

	const auto gated = upload(rom, os, true);
	std::printf("     gated: %zu packets sent, %zu ACKs, last taken %d, link spoke %zu times mid-upload\n", gated.sent, gated.acked, gated.lastTaken, gated.linkSpoke);
	check(gated.linkSpoke == 0, "gated, the settings' link says nothing between the packets");
	check(gated.lastTaken && gated.sent == 2, "gated, both packets go in and the last is taken");

	const auto naked = upload(rom, os, false);
	std::printf("     not gated: %zu packets sent, %zu ACKs, last taken %d, link spoke %zu times mid-upload\n", naked.sent, naked.acked, naked.lastTaken, naked.linkSpoke);
	std::printf("     (for the record) without the gate the link %s the upload\n", naked.linkSpoke ? (naked.lastTaken ? "spoke in it, and the OS took it anyway:" : "spoke in it and the OS dropped") : "never spoke in");

	std::printf(failures ? "%d failure(s)\n" : "all ok\n", failures);
	return failures ? 1 : 0;
}
