#pragma once

// The G1's memory shared by every front end, as one synth has one (Javier, 2026-11-03): the rack's
// flash.bin, the Micro's flash-micro.bin. The standalone and every plugin instance start from it,
// and what one stores goes back into it, so a new instance, a reopened project or the standalone
// sees it. A project keeps what its slots hold, not its own banks.
//
// Only the patch storage is shared (the flash past Engine::OsBytes): each G1 runs the OS it was
// given. Every read and write of the file is under a lock every process sees.
//
// Several G1s at once: each remembers the banks it last read or wrote (its base). One that writes
// while the file still holds its base writes its banks whole. One the file has moved on from (another
// G1 wrote since) writes nothing over it: it is behind, and what it stored from an editor is
// stored again into the file by a G1 of its own (replay), the way an editor would, so both G1s'
// patches end up in the memory and the OS keeps its own storage in order.

#include <cstdint>
#include <string>
#include <vector>

namespace g1app
{
	class Engine;

	class SharedBanks
	{
	public:
		// The file of a model: flash.bin, or flash-micro.bin beside it for the Micro.
		static std::string pathFor(uint32_t _dspCount);

		void setPath(const std::string& _path) { m_path = _path; }
		const std::string& path() const { return m_path; }

		// The shared memory into _engine (Engine::loadFlash), remembered as the base. False when there
		// is no file yet or it cannot be used: _engine keeps its flash, and the first sync writes the file.
		bool loadInto(Engine& _engine);
		// The base without loading: what the front end already put in its G1 (the standalone, which
		// loads the file itself).
		void adoptBase(const std::vector<uint8_t>& _flash);

		enum class Sync { Written, Unchanged, Behind, Failed };
		// _flash is this G1's whole flash. Written when the file still had this G1's base (or there
		// was none); then the base is _flash. Behind when another G1 wrote since: nothing is written.
		// _withOs: the OS part goes too (the standalone's flash.bin always held its OS).
		Sync sync(const std::vector<uint8_t>& _flash, bool _withOs, std::string& _note);
		bool behind() const { return m_behind; }

		// A patch an editor stored: the bank and position, and the slot's patch as an editor uploads it.
		struct Store
		{
			uint8_t bank = 0, position = 0;
			std::vector<std::vector<uint8_t>> sections;
		};
		// For a G1 that is behind: a G1 of the same model and OS boots on the file, takes each patch in
		// slot A and stores it where it was stored, and the result is written. The file is locked for
		// all of it. Takes a few seconds of CPU.
		bool replay(const std::vector<uint8_t>& _rom, const std::vector<uint8_t>& _os, uint32_t _dspCount,
			const std::vector<Store>& _stores, std::string& _note);

		// The PatchHandling Store an editor sends (`41 0B slot bank position`): false for anything else.
		static bool parseStore(const std::vector<uint8_t>& _msg, uint8_t& _slot, uint8_t& _bank, uint8_t& _position);

	private:
		std::string m_path;
		std::vector<uint8_t> m_base;	// the patch storage as this G1 last read or wrote it; empty: none yet
		bool m_behind = false;
	};
}
