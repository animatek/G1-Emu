#pragma once

// Where the ROM comes from. G1-Emu ships no ROM and never will, so the first thing a new user
// meets is this: the emulator has to say what it needs, where to put it, and what is wrong with
// the file they did put there.
//
// It looks, in this order, and takes the first file that passes g1::checkRom:
//   1. the path given on the command line;
//   2. `rom = ...` in the settings file, which the window's file picker writes;
//   3. the ROM folder, the one a user is told about: <Documents>/Animatek/G1-Emu/roms;
//   4. the same folder next to the flash, for anyone who prefers it out of Documents;
//   5. `Roms/` in the current directory and in the source tree, which is how the repo works.
//
// A folder can hold several dumps: every file of the right size is checked, and the ones that
// fail are reported with their reason, because "there is a ROM there and it is still not working"
// is the case worth explaining.

#include "g1Lib/g1rom.h"

#include <string>
#include <vector>

namespace g1app
{
	struct RomSearch
	{
		std::string path;							// the ROM to use, empty if none was found
		std::vector<std::string> looked;			// folders looked into, in order
		std::vector<std::pair<std::string, std::string>> rejected;	// file -> why it was not taken

		bool found() const { return !path.empty(); }
	};

	// <Documents>/Animatek/G1-Emu/roms, honouring the user's XDG document folder (it is not
	// called "Documents" in every language). This is the one the user is told about.
	std::string publicRomFolder();

	// Every folder searched, in order, whether or not it exists. For the "put it here" message.
	std::vector<std::string> romFolders();

	RomSearch findRom(const std::string& _fromCommandLine, const std::string& _fromSettings);

	// Reads a ROM and says what it is, without going near the emulator.
	g1::RomCheck inspectRom(const std::string& _path, std::vector<uint8_t>& _data);
}
