// g1synthsettingstest: the SynthSettingsLink against an emulated G1, with no editor.
//
//   1. The link reads the factory settings once the G1 is up (greeting it, as nothing else has).
//   2. It writes other MIDI channels and globals, and reading them back gives what was written.
//   3. A restart from the user state loses what was written (the OS keeps it in memory). A
//      SlotKeeper puts a patch back into slot A, and then the settings are written back, as the
//      plugin does from the project: they are there again with the first write (the link reads
//      first for slot A's new pid, which the OS checks).
//   4. A section survives encode() and decode() as it was.
//
// Exit code 77 (skipped) without a ROM, 1 on any failure.

#include "engine.h"
#include "hostconfig.h"
#include "romfinder.h"
#include "slotkeeper.h"
#include "synthsettings.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
	constexpr uint64_t g_ms = g1::g_ucClock / 1000;

	// NME's upload of SimpleOSC.pch, as in g1slotkeepertest: a patch for a SlotKeeper to restore.
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

	std::string describe(const g1app::SynthSettings& _s)
	{
		char buf[256];
		std::snprintf(buf, sizeof(buf), "\"%s\" channels %d %d %d %d, clock %s %d bpm, sync %d, tune %d, vel %d-%d, local %d, LEDs %d, kb %d, pedal %d, PC %d/%d, knob %d",
			_s.name.c_str(), _s.midiChannel[0] + 1, _s.midiChannel[1] + 1, _s.midiChannel[2] + 1, _s.midiChannel[3] + 1,
			_s.clockInternal ? "internal" : "external", _s.clockBpm, _s.globalSync, _s.masterTune, _s.velScaleMin, _s.velScaleMax,
			_s.localOn, _s.ledsActive, _s.keyboardMode, _s.pedalPolarity, _s.programChangeReceive, _s.programChangeSend, _s.knobMode);
		return buf;
	}

	bool same(const g1app::SynthSettings& _a, const g1app::SynthSettings& _b)
	{
		return _a.clockInternal == _b.clockInternal && _a.velScaleMin == _b.velScaleMin && _a.velScaleMax == _b.velScaleMax
			&& _a.ledsActive == _b.ledsActive && _a.clockBpm == _b.clockBpm && _a.localOn == _b.localOn
			&& _a.keyboardMode == _b.keyboardMode && _a.pedalPolarity == _b.pedalPolarity && _a.globalSync == _b.globalSync
			&& _a.masterTune == _b.masterTune && _a.programChangeReceive == _b.programChangeReceive
			&& _a.programChangeSend == _b.programChangeSend && _a.knobMode == _b.knobMode && _a.name == _b.name
			&& _a.midiChannel == _b.midiChannel;
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
	auto& mc = engine.mc();
	g1app::SynthSettingsLink link;
	std::vector<uint8_t> fromG1, toEditor, toG1;
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
			toEditor.clear();
			link.g1Sent(fromG1, now, toEditor);
			toG1.clear();
			link.tick(now, toG1);
			if(!toG1.empty())
				mc.getPcPort().receive(toG1);
		}
	};
	auto waitFor = [&](const uint64_t _revision, const uint64_t _maxMs, g1app::SynthSettings& _s)
	{
		uint64_t rev = 0;
		for(uint64_t t = 0; t < _maxMs; t += 50)
		{
			run(50);
			if(link.settings(_s, rev) && rev > _revision)
				return rev;
		}
		return uint64_t(0);
	};

	// 1. The factory settings
	g1app::SynthSettings factory;
	const auto rev1 = waitFor(0, 8000, factory);
	check(rev1 > 0, "read the settings: " + describe(factory));

	// 2. Other channels and globals, and back
	auto changed = factory;
	changed.midiChannel = {4, 9, 15, 2};
	changed.clockBpm = 97;
	changed.globalSync = 8;
	changed.masterTune = -12;
	changed.knobMode = 1;
	changed.velScaleMax = 100;
	link.write(changed);
	g1app::SynthSettings back;
	const auto rev2 = waitFor(rev1, 5000, back);
	check(rev2 > rev1, "read back after writing: " + describe(back));
	check(same(back, changed), "the OS keeps what was written");

	// 3. A project reopened: a new G1 from this one's user state. The OS kept what was written only
	//    in its memory, so the new G1 has the old settings; written back from what the link last
	//    read, as the plugin's project keeps it, they are there again (#46).
	{
		g1app::SynthSettings kept;
		uint64_t keptRevision = 0;
		check(link.settings(kept, keptRevision) && same(kept, changed), "the link has the settings as the OS last said them");
		run(3000);
		g1app::Engine engine2(rom, options.loadOs(osNote));
		std::string err;
		check(engine2.setUserState(engine.userState(), err), "a new G1 takes the user state " + err);
		auto& mc2 = engine2.mc();
		g1app::SynthSettingsLink link2;
		// As the plugin reopens a project: a keeper puts a patch back into slot A after the boot.
		g1app::SlotKeeper keeper2;
		g1app::SlotKeeper::Slots slots;
		slots[0].sections = g_simpleOsc;
		keeper2.restore(slots);
		auto run2 = [&](const uint64_t _ms)
		{
			for(uint64_t i = 0; i < _ms; ++i)
			{
				const auto end = mc2.ucCycles() + g_ms;
				while(mc2.ucCycles() < end)
					mc2.exec();
				const auto now = mc2.ucCycles() / g_ms;
				fromG1.clear();
				mc2.getPcPort().takeTx(fromG1);
				// As the plugin's runner: each sees all the G1 sends, and the other's requests as an editor's.
				toEditor.clear();
				keeper2.g1Sent(fromG1, now, toEditor);
				link2.g1Sent(fromG1, now, toEditor);
				for(int l = 0; l < 2; ++l)
				{
					toG1.clear();
					if(l == 0)
						keeper2.tick(now, toG1);
					else
						link2.tick(now, toG1);
					if(toG1.empty())
						continue;
					mc2.getPcPort().receive(toG1);
					if(l == 0)
						link2.editorSent(toG1, now);
					else
						keeper2.editorSent(toG1, now);
				}
			}
		};
		auto waitFor2 = [&](const uint64_t _revision, g1app::SynthSettings& _s)
		{
			uint64_t rev = 0;
			for(uint64_t t = 0; t < 10000; t += 50)
			{
				run2(50);
				if(link2.settings(_s, rev) && rev > _revision)
					return rev;
			}
			return uint64_t(0);
		};
		g1app::SynthSettings restarted;
		const auto r1 = waitFor2(0, restarted);
		check(r1 > 0 && same(restarted, factory), "after a restart the OS has the old settings: " + describe(restarted));
		bool restored = false;
		for(int t = 0; t < 200 && !restored; ++t)
		{
			run2(50);
			restored = keeper2.settled();
		}
		check(restored, "the keeper puts slot A back");
		link2.write(kept);	// the first write after the slot's upload, as the plugin's
		const bool again = waitFor2(r1, restarted) > r1 && same(restarted, changed);
		check(again, "written back, they are there again: " + describe(restarted));
		// The link's read and write are not a patch change: the keeper has no slot to read again.
		check(keeper2.settled(), "the keeper takes the settings' read and write for no change of slot A");
	}

	// 4. encode/decode
	g1app::SynthSettings round;
	check(g1app::SynthSettings::decode(changed.encode(), round) && same(round, changed), "encode/decode");

	std::printf(failures ? "%d failure(s)\n" : "all ok\n", failures);
	return failures ? 1 : 0;
}
