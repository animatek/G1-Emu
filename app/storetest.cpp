// g1storetest: a patch stored into a bank survives the plugin's state (2026-11-03).
//
// Javier, in Bitwig: a patch made in NME and stored into a bank is in the synth, but after the
// project is reopened (or the plugin loaded again) it is gone, on the rack and on the Micro. This
// is the plugin's path without a DAW: an editor uploads SimpleOSC into slot A and stores it into
// bank 6 (bank 1 on the Micro), position 1 (`41 0B slot bank position`, as NME and Clavia's editor send it); the bank is
// read back; then the G1's userState() (what getStateInformation saves) goes into a new G1 through
// setUserState(), as a reopened project does, and the bank is read again.
//
// Reports when the store reached the flash: the OS may answer before it writes. With G1_MODEL=micro
// (and the Micro's OS: G1_OS, or the settings' microOs) the same on the Micro.
// Exit code 77 (skipped) without a ROM, 1 on failure.

#include "engine.h"
#include "hostconfig.h"
#include "osimage.h"
#include "romfinder.h"
#include "slotkeeper.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace
{
	constexpr uint64_t g_ms = g1::g_ucClock / 1000;
	uint8_t g_bank = 5;	// bank 6 (G1_STORE_BANK, from 0), position 1 as the panel counts
	constexpr uint8_t g_position = 0;

	// NME's upload of SimpleOSC.pch (the same as g1slotkeepertest's).
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

	// Runs the G1 for _ms, keeping what it says on the PC Port.
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

	int acks(const std::vector<uint8_t>& _b)
	{
		int n = 0;
		for(size_t k = 0; k + 5 < _b.size(); ++k)
			n += _b[k] == 0xf0 && _b[k + 1] == 0x33 && (_b[k + 2] >> 2) == 0x16;
		return n;
	}

	// The bank's list, as an editor asks for it, and whether "SimpleOSC" is in the answer.
	bool bankHasIt(g1app::Engine& _e)
	{
		_e.mc().getPcPort().receive(frame(0x17, 0, {0x41, 0x14, g_bank, g_position}));
		std::vector<uint8_t> seen;
		run(_e, 500, &seen);
		const std::string text(seen.begin(), seen.end());
		return text.find("SimpleOSC") != std::string::npos;
	}

	size_t flashDiff(const std::vector<uint8_t>& _a, const std::vector<uint8_t>& _b)
	{
		size_t n = 0;
		for(size_t i = g1app::Engine::OsBytes; i < std::min(_a.size(), _b.size()); ++i)
			n += _a[i] != _b[i];
		return n;
	}
}

int main()
{
	g1app::HostOptions options;
	options.load(g1app::defaultSettingsPath());
	const bool micro = options.micro = [] { const char* m = std::getenv("G1_MODEL"); return m && std::string(m) == "micro"; }();
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
		std::printf("skipped: no Micro OS (%s)\n", osNote.c_str());
		return 77;
	}
	// The Micro's OS takes bank 1 only: a Store into 2-9 is answered with `0D bank 00 00 03` and
	// nothing is written (found here 2026-11-03).
	if(micro)
		g_bank = 0;
	if(const char* b = std::getenv("G1_STORE_BANK"))
		g_bank = static_cast<uint8_t>(std::atoi(b));
	const uint32_t dsps = micro ? g1::g_microDsps : g1::g_regularDsps;
	std::printf("model %s, %s\n", micro ? "micro" : "regular", osNote.empty() ? "OS: the ROM's" : osNote.c_str());

	int failures = 0;
	auto check = [&](const bool _ok, const std::string& _what)
	{
		std::printf("%s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		failures += _ok ? 0 : 1;
	};

	g1app::Engine first(rom, os, dsps);
	run(first, 8000);	// boot, and format the flash on a first start
	check(!bankHasIt(first), "bank " + std::to_string(g_bank + 1) + " does not have SimpleOSC before the store");

	// The upload into slot A, packet by packet on each ACK.
	auto& mc = first.mc();
	const auto packets = g1app::SlotKeeper::uploadMessages(g_simpleOsc, 0);
	size_t acked = 0;
	for(const auto& p : packets)
	{
		mc.getPcPort().receive(p);
		std::vector<uint8_t> seen;
		for(int t = 0; t < 4000 && !acks(seen); t += 10)
			run(first, 10, &seen);
		acked += acks(seen) ? 1 : 0;
	}
	check(acked == packets.size(), "SimpleOSC uploaded into slot A (" + std::to_string(acked) + " of " + std::to_string(packets.size()) + " packets ACKed)");

	// Store, and watch the flash: when does it change, and is the bank read back?
	const auto before = mc.getFlash().data();
	mc.getPcPort().receive(frame(0x17, 0, {0x41, 0x0b, 0x00, g_bank, g_position}));
	std::vector<uint8_t> seen;
	uint64_t answerMs = 0, flashMs = 0;
	for(uint64_t t = 1; t <= 5000 && !(answerMs && flashMs && t > flashMs + 500); ++t)
	{
		run(first, 1, &seen);
		if(!answerMs && !seen.empty())
			answerMs = t;
		if(!flashMs && flashDiff(before, mc.getFlash().data()))
			flashMs = t;
	}
	std::printf("     store answer:");
	for(size_t i = 0; i < seen.size() && i < 40; ++i)
		std::printf(" %02x", seen[i]);
	std::printf("\n");
	std::printf("     store: the G1 answered after %llu ms, the flash changed after %llu ms (%zu bytes)\n",
		static_cast<unsigned long long>(answerMs), static_cast<unsigned long long>(flashMs), flashDiff(before, mc.getFlash().data()));
	check(flashMs != 0, "the store reaches the flash");
	check(bankHasIt(first), "bank " + std::to_string(g_bank + 1) + " has SimpleOSC after the store");

	// What a project saves, into a new G1, as a reopened project does.
	const auto state = first.userState();
	g1app::Engine second(rom, os, dsps);
	std::string error;
	check(second.setUserState(state, error), "the state is taken" + (error.empty() ? std::string() : ": " + error));
	run(second, 8000);
	std::printf("     the restored flash differs from the stored one in %zu bytes\n", flashDiff(first.mc().getFlash().data(), second.mc().getFlash().data()));
	check(bankHasIt(second), "bank " + std::to_string(g_bank + 1) + " has SimpleOSC in a G1 built from the saved state");

	std::printf("%s\n", failures ? "FAILED" : "passed");
	return failures ? 1 : 0;
}
