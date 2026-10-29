#pragma once

// OS images. Clavia's Windows updaters (`Nord Modular OS v3.03b Update.exe`, `Nord Micro Modular
// OS v3.03b Update.exe`) carry the OS as a resource; this reads it out so that a user can point
// G1-Emu at the updater they downloaded, with nothing extracted by hand. A bare image (what
// `os =` has always taken, or what update mode saves) is read as it is.
//
// The payload is "Clavia OS update file V1.0\0" <model> "\0" <major> <minor> 00 00 <length, LE32>
// <image>, each byte i XORed with (0x11 * (i + 1)) & 0xFF and then with its adjacent bit pairs
// swapped. See NOTES.md, "The official OS update".

#include <cstdint>
#include <string>
#include <vector>

namespace g1app
{
	struct OsImage
	{
		std::vector<uint8_t> bytes;		// the OS as it runs from RAM at $100000
		std::string model;				// what the updater says it is for ("Nord Micro Modular"...); empty for a bare image
		int major = 0, minor = 0;
		bool fromUpdater = false;

		bool isMicro() const;			// the model names the Micro Modular
	};

	// The OS inside an updater's bytes. False when they are not one.
	bool extractOsFromUpdater(const std::vector<uint8_t>& _file, OsImage& _out);

	// A file that is an updater or a bare OS image. _why says what is wrong when it returns false.
	bool readOsFile(const std::string& _path, OsImage& _out, std::string& _why);

	// For a file picked as a ROM that is not one: if it is an OS image or an updater (what G1-Mods' builder
	// gives for a Micro, what update mode saves), a sentence saying where it goes. Empty when it is neither.
	std::string osImageHint(const std::string& _path);

	// The Micro Modular's OS: _setting (the settings file's `microOs`, an updater or an image) if it
	// is there, else the first Micro updater or `micro-os.bin` in the ROM folders (and their `Micro`
	// folder). _note says which was used, or what was looked for and not found.
	bool findMicroOs(const std::string& _setting, OsImage& _out, std::string& _note);
}
