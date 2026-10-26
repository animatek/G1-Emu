#pragma once

// Engine: the emulated G1 with nothing around it. A ROM goes in, the factory OS is installed in
// its flash, and from there whoever owns it decides how the flash is kept, where the MIDI comes
// from, where the audio goes and how fast it runs: EmuHost (console and window) against the wall
// clock, the plugin against the DAW's audio blocks. It opens no device, starts no thread of its own
// (the DSP workers are the Microcontroller's) and writes no file.
//
// The flash holds the installed OS, which is the ROM's code, and the user's patches and settings.
// A plugin keeps its G1 inside the DAW project, which may travel, so what it saves is userState():
// only the bytes that differ from the factory flash this ROM produces. The OS never leaves the
// machine that has the ROM. The state names its OS by a hash; restored under another OS, the
// banks and settings still go in and the OS is the one in use.

#include "g1Lib/g1mc.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace g1app
{
	class Engine
	{
	public:
		// The outputs carry the last DSP's X:$5F offset ($155, almost nothing); on the hardware the
		// output capacitor removes it. Whoever sends the audio out subtracts it.
		static constexpr int32_t OutputDc = 0x155;

		// The flash below this is the installed OS; the patch storage starts here.
		static constexpr size_t OsBytes = 0x70000;

		// _rom must be a rack ROM that g1::checkRom accepts (inspectRom in romfinder.h). The flash
		// starts with the factory OS installed and nothing else, as after an update.
		// _os, if given, is the OS image to install instead of the ROM's factory OS (HostOptions::os:
		// Clavia's 3.03b update, which the hardware runs). An image that cannot be one is ignored.
		// _dspCount: 4 for a Regular, 8 for an Expanded (g1::g_regularDsps, g1::g_expandedDsps).
		// _threaded: see g1::Microcontroller (G1_THREADS still wins).
		explicit Engine(const std::vector<uint8_t>& _rom, const std::vector<uint8_t>& _os = {}, uint32_t _dspCount = g1::g_regularDsps, bool _threaded = true);

		// Whether the OS in use is the one given, not the ROM's.
		bool customOs() const { return m_customOs; }
		~Engine();

		g1::Microcontroller& mc() { return *m_mc; }
		const g1::Microcontroller& mc() const { return *m_mc; }

		// The whole flash: the standalone's flash.bin. Its OS (length and image) is replaced with
		// this engine's, so the OS always comes from the ROM or the OS image given, never from the
		// file; the rest, the OS's own marks included, is kept as it is.
		// Before the first exec().
		bool loadFlash(const std::vector<uint8_t>& _image);

		// The flash as a difference against the factory one (patches, synth settings), and back.
		// setUserState() takes a state made with another OS too (the ROM's or an OS image): its
		// banks and settings go in and the OS stays this engine's; _otherOs says so. A state it
		// cannot read leaves the flash untouched, and _error says why. Also before the first exec().
		std::vector<uint8_t> userState() const;
		bool setUserState(const std::vector<uint8_t>& _state, std::string& _error, bool* _otherOs = nullptr);

		// Identifies the factory flash, and so the OS in use, without carrying any of it.
		uint32_t factoryHash() const { return m_factoryHash; }

	private:
		std::unique_ptr<g1::Microcontroller> m_mc;
		std::vector<uint8_t> m_factory;
		uint32_t m_factoryHash = 0;

		void useOwnOs(std::vector<uint8_t>& _flash) const;
		bool m_customOs = false;
	};
}
