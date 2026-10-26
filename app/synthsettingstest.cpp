// g1synthsettingstest: the SynthSettingsLink against an emulated G1, with no editor.
//
//   1. The link reads the factory settings once the G1 is up (greeting it, as nothing else has).
//   2. It writes other MIDI channels and globals, and reading them back gives what was written.
//   3. A restart from the user state loses what was written (the OS keeps it in memory). A
//      SlotKeeper puts a patch back into slot A, and then the settings are written back, as the
//      plugin does from the project: they are there again with the first write (the link reads
//      first for slot A's new pid, which the OS checks).
//   4. A section survives encode() and decode() as it was.
//   5. A write leaves the flash as it was: only Shift+Store stores them, so a G1 booted from that
//      flash has its old ones (issue #46).
//   6. A new G1 given those settings as a restored project's are gives them: written before the
//      link has read anything, they read back as written.
//   7. With setReread, the link keeps up with settings it did not write: an editor's, read back
//      once it is quiet, and ones changed without a word (the panel's System menu), read again
//      within the period.
//   In 3, the keeper puts patches back into slots A and B, and the project's slots (only B on, B
//   active) are written with the settings: they read back as the project had them (issue #75).
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
		std::string r = buf;
		if(_s.hasSlots)
		{
			r += ", slots on ";
			for(size_t i = 0; i < 4; ++i)
				if(_s.slotSelected[i])
					r += static_cast<char>('A' + i);
			r += std::string(", active ") + static_cast<char>('A' + _s.activeSlot);
		}
		return r;
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

	// A G1 and a link on its PC Port, with no editor, run a millisecond at a time.
	struct G1
	{
		G1(const std::vector<uint8_t>& _rom, const std::vector<uint8_t>& _os) : engine(_rom, _os) {}

		g1app::Engine engine;
		g1app::SynthSettingsLink link;
		std::vector<uint8_t> fromG1, toEditor, toG1;

		void run(const uint64_t _ms)
		{
			auto& mc = engine.mc();
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
		}

		uint64_t waitFor(const uint64_t _revision, const uint64_t _maxMs, g1app::SynthSettings& _s)
		{
			uint64_t rev = 0;
			for(uint64_t t = 0; t < _maxMs; t += 50)
			{
				run(50);
				if(link.settings(_s, rev) && rev > _revision)
					return rev;
			}
			return 0;
		}


		// Settings sent to the OS as an editor sends them ($1F), past the link; _editor says whether
		// the link is told, as the runner tells it of what an editor sends.
		void sendAsEditor(const g1app::SynthSettings& _s, const bool _editor)
		{
			auto payload = g1app::pcsysex::pack7(_s.encode());
			payload.insert(payload.begin(), 0);
			const auto msg = g1app::pcsysex::frame(0x1f, 0, payload);
			if(_editor)
				link.editorSent(msg, engine.mc().ucCycles() / g_ms);
			engine.mc().getPcPort().receive(msg);
		}
	};
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
	G1 g1(rom, os);
	auto& link = g1.link;
	const auto waitFor = [&](const uint64_t _revision, const uint64_t _maxMs, g1app::SynthSettings& _s) { return g1.waitFor(_revision, _maxMs, _s); };

	// 1. The factory settings
	g1app::SynthSettings factory;
	const auto rev1 = waitFor(0, 8000, factory);
	check(rev1 > 0, "read the settings: " + describe(factory));
	check(factory.hasSlots, "they say which slots are on and which is active");

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
		g1.run(3000);
		g1app::Engine engine2(rom, options.loadOs(osNote));
		std::string err;
		check(engine2.setUserState(g1.engine.userState(), err), "a new G1 takes the user state " + err);
		auto& mc2 = engine2.mc();
		g1app::SynthSettingsLink link2;
		// As the plugin reopens a project: a keeper puts a patch back into slot A after the boot.
		g1app::SlotKeeper keeper2;
		std::vector<uint8_t> fromG1, toEditor, toG1;
		g1app::SlotKeeper::Slots slots;
		slots[0].sections = g_simpleOsc;
		slots[1].sections = g_simpleOsc;
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
		check(restored, "the keeper puts slots A and B back");
		g1app::SynthSettings afterUpload;
		uint64_t r2 = 0;
		link2.read();
		r2 = waitFor2(r1, afterUpload);
		std::printf("     after the uploads: %s\n", describe(afterUpload).c_str());
		// The project's slots: only B on, and B active (#75).
		auto project = kept;
		project.hasSlots = true;
		project.slotSelected = {false, true, false, false};
		project.activeSlot = 1;
		link2.write(project, true);	// the first write after the slots' upload, as the plugin's
		const bool again = waitFor2(r2, restarted) > r2 && same(restarted, changed);
		check(again, "written back, they are there again: " + describe(restarted));
		check(restarted.hasSlots && restarted == project, "and the slots as the project had them, only B on and active");
		// The link's read and write are not a patch change: the keeper has no slot to read again.
		check(keeper2.settled(), "the keeper takes the settings' read and write for no change of slot A");
	}

	// 4. encode/decode
	g1app::SynthSettings round;
	check(g1app::SynthSettings::decode(changed.encode(), round) && same(round, changed), "encode/decode");
	{
		auto slotsOn = changed;
		slotsOn.hasSlots = true;
		slotsOn.slotSelected = {true, false, true, true};
		slotsOn.activeSlot = 2;
		g1app::SynthSettings back4, bare;
		check(g1app::SynthSettings::decode(slotsOn.encode(), back4) && back4.hasSlots && back4.slotSelected == slotsOn.slotSelected
			&& back4.activeSlot == 2, "encode/decode with the slots");
		check(g1app::SynthSettings::decode(slotsOn.encode(false), bare) && !bare.hasSlots, "as the OS takes them, without the slots");
	}

	// 5. Not in the flash
	const auto stateAfterWrite = g1.engine.userState();
	g1app::SynthSettings unstored;
	{
		G1 reopened(rom, os);
		std::string error;
		reopened.engine.setUserState(stateAfterWrite, error);
		reopened.waitFor(0, 8000, unstored);
	}
	check(!same(unstored, changed), "a write is not stored: a G1 from that flash boots with " + describe(unstored));

	// 6. Given back to a new G1, as a project's state is
	{
		G1 restored(rom, os);
		std::string error;
		restored.engine.setUserState(stateAfterWrite, error);
		restored.link.write(changed);
		g1app::SynthSettings back5;
		const auto rev5 = restored.waitFor(0, 10000, back5);
		check(rev5 > 0 && same(back5, changed), "restored into a new G1: " + describe(back5));
	}

	// 7. Kept up with
	link.setReread(2000);
	auto byEditor = changed;
	byEditor.midiChannel = {6, 7, 11, 13};
	g1.sendAsEditor(byEditor, true);
	g1app::SynthSettings seen;
	auto rev6 = waitFor(rev2, 1500, seen);
	check(rev6 > rev2 && same(seen, byEditor), "an editor's settings, read back: " + describe(seen));
	auto silent = byEditor;
	silent.clockBpm = 133;
	g1.sendAsEditor(silent, false);
	const auto rev7 = waitFor(rev6, 4000, seen);
	check(rev7 > rev6 && same(seen, silent), "settings changed without a word, read again: " + describe(seen));

	std::printf(failures ? "%d failure(s)\n" : "all ok\n", failures);
	return failures ? 1 : 0;
}
