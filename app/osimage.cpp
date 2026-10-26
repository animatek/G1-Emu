#include "osimage.h"

#include "romfinder.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace g1app
{
	namespace
	{
		constexpr char g_tag[] = "Clavia OS update file V1.0";	// followed by its NUL
		// As it runs from RAM at $100000: a few hundred KB of long words, below the patch storage.
		constexpr size_t g_minOsBytes = 0x10000, g_maxOsBytes = 0x6ffe0;

		bool plausibleOs(const std::vector<uint8_t>& _bytes)
		{
			return _bytes.size() >= g_minOsBytes && _bytes.size() <= g_maxOsBytes && (_bytes.size() & 3) == 0;
		}

		bool readFile(const std::string& _path, std::vector<uint8_t>& _out)
		{
			std::ifstream f(_path, std::ios::binary);
			if(!f)
				return false;
			_out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			return true;
		}

		bool isMicroName(std::string _s)
		{
			std::transform(_s.begin(), _s.end(), _s.begin(), [](const unsigned char _c) { return static_cast<char>(std::tolower(_c)); });
			return _s.find("micro") != std::string::npos;
		}
	}

	bool OsImage::isMicro() const
	{
		return isMicroName(model);
	}

	bool extractOsFromUpdater(const std::vector<uint8_t>& _file, OsImage& _out)
	{
		const size_t tagLen = sizeof(g_tag);	// with the NUL
		// The updater's own code holds the same tag with nothing after it; the payload's is followed
		// by the model's name.
		for(size_t at = 0; at + tagLen + 16 < _file.size(); ++at)
		{
			if(std::memcmp(_file.data() + at, g_tag, tagLen) != 0)
				continue;
			const size_t start = at + tagLen;
			if(_file[start] == 0)
				continue;
			const auto nameEnd = std::find(_file.begin() + static_cast<std::ptrdiff_t>(start), _file.end(), uint8_t(0));
			if(nameEnd == _file.end())
				return false;
			const size_t h = static_cast<size_t>(nameEnd - _file.begin()) + 1;
			if(h + 8 > _file.size())
				return false;
			const size_t length = static_cast<size_t>(_file[h + 4]) | static_cast<size_t>(_file[h + 5]) << 8 | static_cast<size_t>(_file[h + 6]) << 16 | static_cast<size_t>(_file[h + 7]) << 24;
			if(h + 8 + length > _file.size())
				return false;
			OsImage os;
			os.model.assign(_file.begin() + static_cast<std::ptrdiff_t>(start), nameEnd);
			os.major = _file[h];
			os.minor = _file[h + 1];
			os.fromUpdater = true;
			os.bytes.resize(length);
			for(size_t i = 0; i < length; ++i)
			{
				const auto x = static_cast<uint8_t>(_file[h + 8 + i] ^ ((0x11 * (i + 1)) & 0xff));
				os.bytes[i] = static_cast<uint8_t>(((x & 0xaa) >> 1) | ((x & 0x55) << 1));
			}
			_out = std::move(os);
			return true;
		}
		return false;
	}

	bool readOsFile(const std::string& _path, OsImage& _out, std::string& _why)
	{
		std::vector<uint8_t> file;
		if(!readFile(_path, file) || file.empty())
		{
			_why = "cannot read it";
			return false;
		}
		OsImage os;
		if(file.size() > 2 && file[0] == 'M' && file[1] == 'Z')
		{
			if(!extractOsFromUpdater(file, os))
			{
				_why = "a Windows program with no Clavia OS inside";
				return false;
			}
		}
		else
			os.bytes = std::move(file);
		if(!plausibleOs(os.bytes))
		{
			_why = "not an OS image (" + std::to_string(os.bytes.size()) + " bytes)";
			return false;
		}
		_out = std::move(os);
		return true;
	}

	bool findMicroOs(const std::string& _setting, OsImage& _out, std::string& _note)
	{
		_note.clear();
		std::string why;
		if(!_setting.empty())
		{
			if(readOsFile(_setting, _out, why))
			{
				_note = "Micro OS: " + _setting;
				return true;
			}
			_note = "Micro OS " + _setting + " not used: " + why + "\n";
		}
		std::vector<std::string> folders;
		for(const auto& dir : romFolders())
		{
			folders.push_back(dir);
			folders.push_back((std::filesystem::path(dir) / "Micro").string());
		}
		for(const auto& dir : folders)
		{
			std::error_code ec;
			if(!std::filesystem::is_directory(dir, ec))
				continue;
			std::vector<std::string> candidates;
			for(const auto& entry : std::filesystem::directory_iterator(dir, ec))
			{
				const auto name = entry.path().filename().string();
				const auto ext = entry.path().extension().string();
				if(entry.is_regular_file(ec) && (name == "micro-os.bin" || (isMicroName(name) && (ext == ".exe" || ext == ".EXE"))))
					candidates.push_back(entry.path().string());
			}
			std::sort(candidates.begin(), candidates.end());
			for(const auto& path : candidates)
			{
				OsImage os;
				if(readOsFile(path, os, why) && (!os.fromUpdater || os.isMicro()))
				{
					_out = std::move(os);
					_note += "Micro OS: " + path;
					return true;
				}
			}
		}
		_note += "No Micro Modular OS found: put Clavia's Micro Modular OS updater (the .exe) in " + publicRomFolder() + " or pick it in Settings";
		return false;
	}
}
