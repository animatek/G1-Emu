#include "romfinder.h"

#include "emuhost.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace g1app
{
	namespace
	{
		std::string home()
		{
			const char* h = std::getenv("HOME");
			return h ? h : ".";
		}

		// The user's document folder. XDG_DOCUMENTS_DIR wins, then the line the desktop writes in
		// ~/.config/user-dirs.dirs (it is "Documentos", "Dokumente", ... depending on the
		// language), then ~/Documents as the last resort.
		std::string documentsFolder()
		{
			if(const char* env = std::getenv("XDG_DOCUMENTS_DIR"); env && *env)
				return env;

			std::ifstream f(home() + "/.config/user-dirs.dirs");
			std::string line;
			while(std::getline(f, line))
			{
				const std::string key = "XDG_DOCUMENTS_DIR=";
				const auto at = line.find(key);
				if(at == std::string::npos || line.find('#') < at)
					continue;
				auto value = line.substr(at + key.size());
				if(!value.empty() && value.front() == '"')
					value = value.substr(1, value.rfind('"') - 1);
				const std::string prefix = "$HOME/";
				if(value.rfind(prefix, 0) == 0)
					value = home() + "/" + value.substr(prefix.size());
				if(!value.empty())
					return value;
			}
			return home() + "/Documents";
		}

		// Where the repo keeps its own ROMs, so working from a clone needs no setup at all.
		void addSourceFolders(std::vector<std::string>& _out)
		{
			std::error_code ec;
			_out.push_back((std::filesystem::current_path(ec) / "Roms").string());
#ifdef G1_SOURCE_DIR
			_out.push_back((std::filesystem::path(G1_SOURCE_DIR) / "Roms").string());
#endif
		}
	}

	std::string publicRomFolder()
	{
		return (std::filesystem::path(documentsFolder()) / "Animatek" / "G1-Emu" / "roms").string();
	}

	std::vector<std::string> romFolders()
	{
		std::vector<std::string> out;
		out.push_back(publicRomFolder());
		out.push_back((std::filesystem::path(EmuHost::defaultFlashPath()).parent_path() / "roms").string());
		addSourceFolders(out);
		out.erase(std::unique(out.begin(), out.end()), out.end());
		return out;
	}

	g1::RomCheck inspectRom(const std::string& _path, std::vector<uint8_t>& _data)
	{
		_data.clear();
		std::error_code ec;
		const auto size = std::filesystem::file_size(_path, ec);
		if(ec || size != g1::g_romSize)		// do not read a DVD image to find out it is not a ROM
		{
			g1::RomCheck r;
			r.size = ec ? 0 : static_cast<size_t>(size);
			return r;
		}
		std::ifstream f(_path, std::ios::binary);
		if(f)
			_data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		return g1::checkRom(_data);
	}

	RomSearch findRom(const std::string& _fromCommandLine, const std::string& _fromSettings)
	{
		RomSearch result;
		std::vector<uint8_t> data;

		auto tryNamed = [&](const std::string& _named)
		{
			const auto check = inspectRom(_named, data);
			if(check.ok())
				result.path = _named;
			else
			{
				std::error_code ec;
				result.rejected.emplace_back(_named, std::filesystem::exists(_named, ec) ? check.what() : "no such file");
			}
			return check.ok();
		};

		// A ROM named on the command line is an order: if it does not serve, say so and stop.
		// Starting with a different one than the one that was asked for is worse than not
		// starting, because it looks like it worked.
		if(!_fromCommandLine.empty())
		{
			tryNamed(_fromCommandLine);
			return result;
		}

		// The one in the settings is a preference, not an order: if it has been moved or deleted
		// since, carry on looking and let the caller report what happened to it.
		if(!_fromSettings.empty() && tryNamed(_fromSettings))
			return result;

		for(const auto& folder : romFolders())
		{
			result.looked.push_back(folder);
			std::error_code ec;
			std::vector<std::string> files;
			for(const auto& entry : std::filesystem::directory_iterator(folder, ec))
				if(entry.is_regular_file(ec))
					files.push_back(entry.path().string());
			std::sort(files.begin(), files.end());		// same answer on every machine

			for(const auto& file : files)
			{
				const auto check = inspectRom(file, data);
				if(check.ok())
				{
					result.path = file;
					return result;
				}
				// A file of the wrong size in a folder full of things is noise; one that is the
				// right size and still not the ROM is what the user needs told.
				if(check.verdict != g1::RomCheck::Verdict::WrongSize)
					result.rejected.emplace_back(file, check.what());
			}
		}
		return result;
	}
}
