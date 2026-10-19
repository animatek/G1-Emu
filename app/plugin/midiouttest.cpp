// g1midiouttest: the G1's MIDI OUT bytes put back into messages for the host, with no ROM and no DAW.
//
// Notes and controllers with and without running status, a real-time byte in the middle of a
// message, program changes, system common messages, and SysEx skipped without losing what follows.

#include "midioutparser.h"

#include <cstdio>
#include <vector>

namespace
{
	int g_failed = 0;

	using Msg = std::vector<uint8_t>;

	std::vector<Msg> parse(const std::vector<uint8_t>& _bytes)
	{
		g1app::MidiOutParser p;
		std::vector<Msg> out;
		g1app::MidiOutParser::Message m;
		for(const auto b : _bytes)
			if(p.feed(b, m))
				out.emplace_back(m.bytes, m.bytes + m.size);
		return out;
	}

	void expect(const char* _what, const std::vector<uint8_t>& _in, const std::vector<Msg>& _want)
	{
		const auto got = parse(_in);
		if(got == _want)
		{
			std::printf("ok   %s\n", _what);
			return;
		}
		++g_failed;
		std::printf("FAIL %s: got", _what);
		for(const auto& m : got)
		{
			std::printf(" [");
			for(const auto b : m)
				std::printf(" %02x", b);
			std::printf(" ]");
		}
		std::printf("\n");
	}
}

int main()
{
	expect("note on and off", {0x90, 60, 100, 0x80, 60, 64}, {{0x90, 60, 100}, {0x80, 60, 64}});
	expect("running status", {0x91, 60, 100, 62, 90, 60, 0}, {{0x91, 60, 100}, {0x91, 62, 90}, {0x91, 60, 0}});
	expect("clock inside a note", {0x90, 60, 0xf8, 100}, {{0xf8}, {0x90, 60, 100}});
	expect("controller and program change", {0xb2, 7, 127, 0xc2, 5, 6}, {{0xb2, 7, 127}, {0xc2, 5}, {0xc2, 6}});
	expect("song position, then no running status", {0xf2, 0x10, 0x01, 0x22}, {{0xf2, 0x10, 0x01}});
	expect("SysEx skipped", {0xf0, 0x33, 0x01, 0x02, 0xf7, 0x90, 60, 100}, {{0x90, 60, 100}});
	expect("SysEx ended by a status", {0xf0, 0x33, 0x01, 0x90, 60, 100}, {{0x90, 60, 100}});
	expect("data before any status", {60, 100, 0xfa, 0x90, 64, 1}, {{0xfa}, {0x90, 64, 1}});
	expect("tune request", {0xf6}, {{0xf6}});

	if(g_failed)
		std::printf("%d failed\n", g_failed);
	return g_failed ? 1 : 0;
}
