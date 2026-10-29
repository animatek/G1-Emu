#pragma once

// Which OS runs, as far as its own version tells: the minor it says in its IAm, read from the
// instruction that builds it, move.b #minor,(8,a2), at $10B288 in the ROM's factory OS, $20
// higher in Clavia's 3.03b update (NOTES.md, "The official OS update") and at $10486E in the Micro
// Modular's 3.03b (G1-Mods' NOTES.md, "The Micro Modular"). All of Clavia's say 3.
// An OS changed to say otherwise is not Clavia's as it came. Read from RAM like g1knobs.h, from
// any thread.

#include "g1mc.h"

namespace g1
{
	// -1 when neither build's instruction is there: another OS, or one not booted yet.
	inline int osVersionMinor(Microcontroller& _mc)
	{
		for(const uint32_t a : {0x10b288u, 0x10b2a8u, 0x10486eu})
		{
			const uint16_t imm = _mc.read16(a + 2);
			if(_mc.read16(a) == 0x157c && imm < 0x100 && _mc.read16(a + 4) == 0x0008)
				return imm;
		}
		return -1;
	}

	inline bool osModified(Microcontroller& _mc)
	{
		const int minor = osVersionMinor(_mc);
		return minor >= 0 && minor != 3;
	}
}
