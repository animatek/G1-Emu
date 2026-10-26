// The OS reader: the Micro Modular's OS out of Clavia's updater, byte for byte what the extraction
// script gives, and the rejections. Needs the updater, which G1-Emu does not ship, so without it
// the test reports itself skipped.

#include "osimage.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace
{
	void check(const bool _ok, const char* _message)
	{
		if(!_ok)
		{
			std::fprintf(stderr, "FAIL: %s\n", _message);
			std::exit(1);
		}
	}
}

int main()
{
	g1app::OsImage micro;
	std::string note;
	if(!g1app::findMicroOs("", micro, note))
	{
		std::printf("skipped: %s\n", note.c_str());
		return 77;
	}
	std::printf("%s\n", note.c_str());
	check(micro.bytes.size() >= 0x10000 && micro.bytes.size() <= 0x6ffe0 && (micro.bytes.size() & 3) == 0, "the OS has a plausible size");
	if(micro.fromUpdater)
	{
		check(micro.isMicro(), "the updater says Micro Modular");
		check(micro.major == 3, "OS 3.x");
	}

	// The same updater read through the explicit setting finds the same image.
	const auto path = note.substr(note.find(": ") + 2);
	g1app::OsImage again;
	std::string why;
	check(g1app::readOsFile(path, again, why), "readOsFile reads what findMicroOs found");
	check(again.bytes == micro.bytes, "the same bytes");

	// A bare image next to it, if there is one (Roms/Micro/micro-os.bin, made by extract-os.py), is the same OS.
	const auto bare = std::filesystem::path(path).parent_path() / "micro-os.bin";
	if(micro.fromUpdater && std::filesystem::exists(bare))
	{
		g1app::OsImage image;
		check(g1app::readOsFile(bare.string(), image, why), "the bare image reads");
		check(!image.fromUpdater, "a bare image is not an updater");
		check(image.bytes == micro.bytes, "the updater gives the script's image");
	}

	// Rejections: a file that is nothing, and one that is a Windows program with no OS inside.
	const auto tmp = std::filesystem::temp_directory_path() / "g1-osimagetest.bin";
	{
		std::ofstream f(tmp, std::ios::binary);
		f << "MZ this is not an updater";
	}
	check(!g1app::readOsFile(tmp.string(), again, why), "a program with no OS is refused");
	{
		std::ofstream f(tmp, std::ios::binary);
		f << "tiny";
	}
	check(!g1app::readOsFile(tmp.string(), again, why), "a tiny file is refused");
	std::filesystem::remove(tmp);
	check(!g1app::readOsFile((std::filesystem::temp_directory_path() / "g1-no-such-file").string(), again, why), "a missing file is refused");

	std::printf("osimagetest: ok\n");
	return 0;
}
