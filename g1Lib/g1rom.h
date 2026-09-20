#pragma once

// Is this file the ROM G1-Emu needs? No ROM is included with G1-Emu and none will be, so the
// file always comes from the user and the answer has to say what is wrong with it, not just no.
//
// Three things identify it, and the OS itself uses the last two:
//   - 512 KB exactly (g_romSize).
//   - "NORD MODULAR", which the ROM carries among the DSP data at $50311 (see NOTES.md).
//   - the model byte at $7FF, which the OS reads at boot and sends to the editor: $01 is the
//     rack, and the rack is the one this emulator runs.

#include "g1mc.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace g1
{
	struct RomCheck
	{
		enum class Verdict
		{
			Ok,
			WrongSize,			// not 512 KB: not this ROM, or a truncated dump
			NotNordModular,		// 512 KB but no trace of a Nord Modular OS
			NotTheRack			// a Nord Modular OS, but the keyboard model's
		};

		Verdict verdict = Verdict::WrongSize;
		size_t size = 0;
		uint8_t model = 0;		// the byte at $7FF: 1 = rack

		bool ok() const { return verdict == Verdict::Ok; }

		// One line for the user, which has to be enough to know what to do next.
		std::string what() const
		{
			switch(verdict)
			{
			case Verdict::Ok:
				return "the Nord Modular rack ROM";
			case Verdict::WrongSize:
				return "not 512 KB (it is " + std::to_string(size) + " bytes): this is not the ROM, or the dump is incomplete";
			case Verdict::NotNordModular:
				return "512 KB but no Nord Modular OS inside: this is some other ROM";
			case Verdict::NotTheRack:
				return "a Nord Modular OS, but not the rack's (model byte " + std::to_string(model)
					+ ", the rack is 1): G1-Emu runs the rack";
			}
			return "unknown";
		}
	};

	inline RomCheck checkRom(const std::vector<uint8_t>& _rom)
	{
		RomCheck r;
		r.size = _rom.size();
		if(_rom.size() != g_romSize)
			return r;

		static constexpr char g_mark[] = "NORD MODULAR";
		const auto* const first = reinterpret_cast<const char*>(_rom.data());
		if(std::search(first, first + _rom.size(), g_mark, g_mark + sizeof(g_mark) - 1) == first + _rom.size())
		{
			r.verdict = RomCheck::Verdict::NotNordModular;
			return r;
		}

		r.model = _rom[0x7ff];
		r.verdict = r.model == 1 ? RomCheck::Verdict::Ok : RomCheck::Verdict::NotTheRack;
		return r;
	}
}
