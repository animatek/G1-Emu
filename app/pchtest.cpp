// g1pchtest: the Presets page's Load .pch against an emulated G1, with no editor.
//
// tools/patches/ClockTest.pch (or G1_PCH) is read and made into packets with Animatek NME's code (PchUpload),
// uploaded into slot A by the PresetsLink as an editor would, and stored in bank 9 at 99; then the
// bank is read again and must have its name there, and the G1's display shows it in slot A.
// Then three more uploads into free places of bank 9: with no editor, with NME connecting in the middle
// of one (its greeting and a RequestPatch) straight in, and with those bytes held until it ends. A
// bigger patch (G1_PCH, 6 or 8 packets) makes the editor land between packets; the repo's own ones
// are a single packet. The OS took them all (2026-10-24), so nothing holds the editor's bytes for the
// Presets page's uploads; this guards that.
// With G1_FLASH (or the standalone's flash.bin) the banks start as that flash has them.
//
// Exit code 77 (skipped) without a ROM, 1 on any failure.

#include "engine.h"
#include "hostconfig.h"
#include "presetslink.h"
#include "romfinder.h"
#include "gui/PchUpload.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>

int main()
{
	int failures = 0;
	auto check = [&](const bool _ok, const std::string& _what)
	{
		std::printf("%s %s\n", _ok ? "ok  " : "FAIL", _what.c_str());
		if(!_ok)
			++failures;
	};

	const char* pch = std::getenv("G1_PCH");	// another patch, a bigger one (several packets)
	const juce::File file(pch ? juce::String(pch) : juce::String(G1_SOURCE_DIR "/tools/patches/ClockTest.pch"));
	const auto up = g1gui::preparePch(file, 0);
	check(up.ok(), file.getFileName().toStdString() + " made into " + std::to_string(up.frames.size()) + " packets, named \"" + up.name.toStdString() + "\"" + (up.error.isEmpty() ? "" : ": " + up.error.toStdString()));

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
		std::printf("no ROM: the upload is not tried\n");
		return failures ? 1 : 77;
	}
	std::string osNote;
	g1app::Engine engine(rom, options.loadOs(osNote));
	const char* flashPath = std::getenv("G1_FLASH");
	std::ifstream ff(flashPath ? flashPath : g1app::defaultFlashPath(), std::ios::binary);
	const std::vector<uint8_t> flash((std::istreambuf_iterator<char>(ff)), std::istreambuf_iterator<char>());
	if(!flash.empty())
		engine.loadFlash(flash);

	auto& mc = engine.mc();
	constexpr uint64_t ms = g1::g_ucClock / 1000;
	g1app::PresetsLink link;
	std::vector<uint8_t> fromG1, toEditor, toG1, seen;	// seen: all that reached the editor
	auto run = [&](const uint64_t _ms)
	{
		for(uint64_t i = 0; i < _ms; ++i)
		{
			const auto end = mc.ucCycles() + ms;
			while(mc.ucCycles() < end)
				mc.exec();
			const auto now = mc.ucCycles() / ms;
			fromG1.clear();
			mc.getPcPort().takeTx(fromG1);
			toEditor.clear();
			link.g1Sent(fromG1, now, toEditor);
			seen.insert(seen.end(), toEditor.begin(), toEditor.end());
			toG1.clear();
			link.tick(now, toG1);
			if(!toG1.empty())
				mc.getPcPort().receive(toG1);
		}
	};
	run(2000);
	mc.getPcPort().receive({0xf0, 0x33, 0x00, 0x06, 0x00, 0x03, 0x03, 0xf7});	// IAm, as an editor greets it
	run(500);

	seen.clear();
	link.upload(0, up.frames, up.abort, 8, 98);
	g1app::PresetsLink::UploadStatus status;
	for(int t = 0; t < 400; ++t)
	{
		run(50);
		status = link.uploadStatus();
		if(!status.busy && status.serial > 1)
			break;
	}
	check(status.ok && !status.busy, "uploaded and stored: " + status.message);
	bool told = false;
	for(size_t i = 0; i + 7 < seen.size(); ++i)
		told = told || (seen[i] == 0xf0 && seen[i + 1] == 0x33 && (seen[i + 2] >> 2) == 0x14 && seen[i + 5] == 0x38 && seen[i + 6] == 0);
	check(told, "the editor is told of the new patch in slot A (NewPatchInSlot)");

	g1app::PresetsLink::BankNames names;
	bool known = false;
	for(int t = 0; t < 200 && !known; ++t)
	{
		run(50);
		known = link.bank(8, names);
	}
	check(known && names[98] == up.name.toStdString(), "bank 9 read again: at 99 \"" + names[98] + "\"");
	std::string shown = mc.getLcd().line(0);
	while(!shown.empty() && shown.back() == ' ')
		shown.pop_back();
	check(shown.find(up.name.toStdString()) != std::string::npos, "the display shows \"" + shown + "\"");

	// A second upload, into slot A at an empty position of bank 9, with NME connecting in the middle of it: its
	// greeting and its RequestPatch reach the PC Port between two packets, as they do when it is
	// open beside the Presets page. Held until the upload ends, as the runner and the standalone do
	// (_hold), or straight in, as they did (#75's failure: the OS drops an upload another message
	// comes into).
	const std::vector<uint8_t> nmeConnects = {0xf0, 0x33, 0x00, 0x06, 0x00, 0x03, 0x03, 0xf7, 0xf0, 0x33, 0x5d, 0x06, 0x41, 0x35, 0x7c, 0xf7};
	const auto uploadWithEditor = [&](const int _mode, const int _position)
	{
		const bool _hold = _mode == 2;
		std::vector<uint8_t> held;
		link.upload(0, up.frames, up.abort, 8, _position);
		bool injected = false;
		uint64_t uploadingSince = 0;
		g1app::PresetsLink::UploadStatus st;
		for(uint64_t t = 0; t < 20000; ++t)
		{
			const auto end = mc.ucCycles() + ms;
			while(mc.ucCycles() < end)
				mc.exec();
			const auto now = mc.ucCycles() / ms;
			if(link.uploading() && !uploadingSince)
				uploadingSince = now;
			if(_mode == 0 && uploadingSince)
				injected = true;		// no editor: the baseline
			if(!injected && link.uploading() && now >= uploadingSince + 20)	// a packet or two in, and still going
			{
				injected = true;
				if(_hold)
					held = nmeConnects;
				else
				{
					mc.getPcPort().receive(nmeConnects);
					link.editorSent(nmeConnects, now);
				}
			}
			if(!held.empty() && !link.uploading())
			{
				mc.getPcPort().receive(held);
				link.editorSent(held, now);
				held.clear();
			}
			fromG1.clear();
			mc.getPcPort().takeTx(fromG1);
			toEditor.clear();
			link.g1Sent(fromG1, now, toEditor);
			toG1.clear();
			link.tick(now, toG1);
			if(!toG1.empty())
				mc.getPcPort().receive(toG1);
			st = link.uploadStatus();
			if(injected && !st.busy && !link.uploading() && held.empty())
				break;
		}
		return std::make_pair(injected, st);
	};
	for(const int mode : {0, 1, 2})
	{
		const bool hold = mode == 2;
		const int position = 91 - mode;
		const auto [injected, st] = uploadWithEditor(mode, position);
		g1app::PresetsLink::BankNames after;
		bool stored = false;
		link.readBank(8);	// the names it had are the ones before the store
		for(int t = 0; t < 200 && !stored; ++t)
		{
			run(50);
			stored = link.bank(8, after) && after[static_cast<size_t>(position)] == up.name.toStdString();
		}
		const std::string what = std::string("an upload with ") + (mode == 0 ? "no editor" : hold ? "the editor connecting in the middle, held until it ends" : "the editor connecting in the middle, straight in") + ": " + st.message + (stored ? " (stored)" : " (not stored)");
		if(mode == 1)	// the OS takes it as it is: a guard, so a change that breaks that is seen
			check(!injected || (st.ok && stored), what);
		else
			check(injected && st.ok && stored, what);
	}

	std::printf(failures ? "%d failure(s)\n" : "all ok\n", failures);
	return failures ? 1 : 0;
}
