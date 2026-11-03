#include "sharedbanks.h"

#include "engine.h"
#include "hostconfig.h"
#include "slotkeeper.h"
#include "filelock.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>


namespace g1app
{
	namespace
	{
		constexpr uint64_t g_ms = g1::g_ucClock / 1000;

		bool readFile(const std::string& _path, std::vector<uint8_t>& _out)
		{
			std::ifstream f(_path, std::ios::binary);
			if(!f)
				return false;
			_out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
			return _out.size() == g1::Flash::Size;
		}

		bool writeFile(const std::string& _path, const std::vector<uint8_t>& _data)
		{
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(_path).parent_path(), ec);
			const auto tmp = _path + ".tmp";
			{
				std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
				if(!f)
					return false;
				f.write(reinterpret_cast<const char*>(_data.data()), static_cast<std::streamsize>(_data.size()));
				if(!f)
					return false;
			}
			std::filesystem::rename(tmp, _path, ec);
			return !ec;
		}

		std::vector<uint8_t> patchArea(const std::vector<uint8_t>& _flash)
		{
			if(_flash.size() <= Engine::OsBytes)
				return {};
			return {_flash.begin() + static_cast<long>(Engine::OsBytes), _flash.end()};
		}

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
		void run(Engine& _e, const uint64_t _ms, std::vector<uint8_t>& _seen)
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
				_seen.insert(_seen.end(), rx.begin(), rx.end());
			}
			mc.syncDsps();
		}

		bool hasMessage(const std::vector<uint8_t>& _b, const uint8_t _cc, const int _type = -1)
		{
			for(size_t k = 0; k + 5 < _b.size(); ++k)
				if(_b[k] == 0xf0 && _b[k + 1] == 0x33 && (_b[k + 2] >> 2) == _cc && (_type < 0 || _b[k + 5] == _type))
					return true;
			return false;
		}
	}

	std::string SharedBanks::pathFor(const uint32_t _dspCount)
	{
		const std::filesystem::path flash(defaultFlashPath());
		return _dspCount == g1::g_microDsps ? (flash.parent_path() / "flash-micro.bin").string() : flash.string();
	}

	bool SharedBanks::loadInto(Engine& _engine)
	{
		m_behind = false;
		std::vector<uint8_t> file;
		{
			FileLock lock(m_path + ".lock");
			if(!readFile(m_path, file))
			{
				m_base.clear();
				return false;
			}
		}
		if(!_engine.loadFlash(file))
		{
			m_base.clear();
			return false;
		}
		m_base = patchArea(file);
		return true;
	}

	void SharedBanks::adoptBase(const std::vector<uint8_t>& _flash)
	{
		m_base = patchArea(_flash);
		m_behind = false;
	}

	SharedBanks::Sync SharedBanks::sync(const std::vector<uint8_t>& _flash, const bool _withOs, std::string& _note)
	{
		const auto mine = patchArea(_flash);
		if(mine.empty())
			return Sync::Failed;
		// Nothing changed here since this G1 read or wrote the memory: nothing to put back, whatever
		// others did meanwhile (the standalone closing after a plugin stored must not count as behind).
		if(!m_base.empty() && mine == m_base)
			return Sync::Unchanged;
		FileLock lock(m_path + ".lock");
		if(!lock.locked())
		{
			_note = "the shared memory " + m_path + " could not be locked";
			return Sync::Failed;
		}
		std::vector<uint8_t> file;
		const bool exists = readFile(m_path, file);
		if(exists)
		{
			const auto theirs = patchArea(file);
			if(theirs == mine)
			{
				m_base = mine;
				return Sync::Unchanged;
			}
			// Another G1 wrote since this one read or wrote it: nothing is written over that.
			if(!m_base.empty() && theirs != m_base)
			{
				m_behind = true;
				return Sync::Behind;
			}
			if(m_base.empty())
			{
				// This G1 never read the file (it was made when there was none): the file wins.
				m_behind = true;
				return Sync::Behind;
			}
		}
		std::vector<uint8_t> out = exists && !_withOs ? file : _flash;
		std::copy(mine.begin(), mine.end(), out.begin() + static_cast<long>(Engine::OsBytes));
		if(!writeFile(m_path, out))
		{
			_note = "the shared memory " + m_path + " could not be written";
			return Sync::Failed;
		}
		m_base = mine;
		return Sync::Written;
	}

	bool SharedBanks::replay(const std::vector<uint8_t>& _rom, const std::vector<uint8_t>& _os, const uint32_t _dspCount,
		const std::vector<Store>& _stores, std::string& _note)
	{
		if(_stores.empty())
			return true;
		FileLock lock(m_path + ".lock");
		std::vector<uint8_t> file;
		if(!lock.locked() || !readFile(m_path, file))
		{
			_note = "the shared memory " + m_path + " could not be read to store into it";
			return false;
		}
		Engine engine(_rom, _os, _dspCount, true);
		if(!engine.loadFlash(file))
		{
			_note = "the shared memory " + m_path + " is not a flash this G1 can run on";
			return false;
		}
		auto& mc = engine.mc();
		std::vector<uint8_t> seen;
		// Up when it answers an editor's greeting.
		bool up = false;
		for(int t = 0; t < 150 && !up; ++t)
		{
			if(t % 5 == 0)
				mc.getPcPort().receive({0xf0, 0x33, 0x00, 0x06, 0x00, 0x03, 0x03, 0xf7});
			run(engine, 100, seen);
			up = hasMessage(seen, 0x00);
		}
		if(!up)
		{
			_note = "the G1 storing into the shared memory did not start";
			return false;
		}
		run(engine, 500, seen);
		int stored = 0;
		std::string failed;
		for(const auto& s : _stores)
		{
			if(s.sections.empty())
			{
				failed += " " + std::to_string(s.bank + 1) + ":" + std::to_string(s.position + 1) + " (slot unknown)";
				continue;
			}
			bool ok = true;
			for(const auto& p : SlotKeeper::uploadMessages(s.sections, 0))
			{
				seen.clear();
				mc.getPcPort().receive(p);
				int waited = 0;
				while(!hasMessage(seen, 0x16) && waited < 4000)
				{
					run(engine, 10, seen);
					waited += 10;
				}
				ok = ok && waited < 4000;
			}
			if(!ok)
			{
				failed += " " + std::to_string(s.bank + 1) + ":" + std::to_string(s.position + 1) + " (upload)";
				continue;
			}
			seen.clear();
			const auto before = mc.getFlash().programmedBytes();
			mc.getPcPort().receive(frame(0x17, 0, {0x41, 0x0b, 0x00, s.bank, s.position}));
			// The answer is a one-entry patch list ($13); a refusal is $0D (a Micro's bank 2-9).
			int waited = 0;
			while(!hasMessage(seen, 0x16) && waited < 3000)
			{
				run(engine, 10, seen);
				waited += 10;
			}
			run(engine, 300, seen);
			if(hasMessage(seen, 0x16, 0x13) && mc.getFlash().programmedBytes() != before)
				++stored;
			else
				failed += " " + std::to_string(s.bank + 1) + ":" + std::to_string(s.position + 1) + " (refused)";
		}
		auto out = file;
		const auto& d = mc.getFlash().data();
		std::copy(d.begin() + static_cast<long>(Engine::OsBytes), d.end(), out.begin() + static_cast<long>(Engine::OsBytes));
		if(!writeFile(m_path, out))
		{
			_note = "the shared memory " + m_path + " could not be written";
			return false;
		}
		_note = std::to_string(stored) + " of " + std::to_string(_stores.size()) + " stores put into the shared memory"
			+ (failed.empty() ? std::string() : ", not:" + failed);
		return failed.empty();
	}

	bool SharedBanks::parseStore(const std::vector<uint8_t>& _m, uint8_t& _slot, uint8_t& _bank, uint8_t& _position)
	{
		if(_m.size() < 11 || _m[0] != 0xf0 || _m[1] != 0x33 || (_m[2] >> 2) != 0x17 || _m[4] != 0x41 || _m[5] != 0x0b)
			return false;
		_slot = _m[6] & 3;
		_bank = _m[7];
		_position = _m[8];
		return true;
	}
}
