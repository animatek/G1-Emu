// g1patchtest: test bench for the emulated G1, with no window and no editor.
//
//   g1patchtest ROM patch.pch [--note 60] [--seconds 2] [--wav output.wav]
//
// Boots the OS, greets like NME (IAm), uploads the patch with the same code NME uses
// (PchFileIO -> PatchSerializer -> UploadPacketizer), packet by packet waiting for each
// reply, plays a note through the PC Port and measures the four outputs and the DSP links.
// Useful to test module by module what sounds and what does not, without touching anyone's G1.
#include "g1Lib/g1mc.h"
#include "g1Lib/g1dsp.h"

#include "model/ModuleDescriptions.h"
#include "model/Patch.h"
#include "model/PatchSerializer.h"
#include "model/PchFileIO.h"
#include "midi/UploadPacketizer.h"
#include "protocol/KnobAssignmentMessage.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	constexpr uint64_t g_ms = g1::g_ucClock / 1000;
	constexpr int32_t g_silence = 0x155;	// DSP 3's X:$5F: what comes out with no signal

	void run(g1::Microcontroller& _mc, const uint64_t _ucCycles)
	{
		const auto end = _mc.ucCycles() + _ucCycles;
		while(_mc.ucCycles() < end)
			_mc.exec();
	}

	std::string hex(const std::vector<uint8_t>& _b, const size_t _max = 24)
	{
		std::string s;
		char buf[4];
		for(size_t i = 0; i < _b.size() && i < _max; ++i)
		{
			std::snprintf(buf, sizeof(buf), "%02x ", _b[i]);
			s += buf;
		}
		if(_b.size() > _max)
			s += "...";
		return s;
	}

	// Sends a message through the PC Port and runs until the OS answers (or _timeoutMs passes).
	std::vector<uint8_t> transact(g1::Microcontroller& _mc, const std::vector<uint8_t>& _msg, const uint32_t _timeoutMs = 300)
	{
		_mc.getPcPort().receive(_msg);
		std::vector<uint8_t> out;
		for(uint32_t t = 0; t < _timeoutMs; ++t)
		{
			run(_mc, g_ms);
			_mc.getPcPort().takeTx(out);
			if(!out.empty() && out.back() == 0xf7)
			{
				run(_mc, 5 * g_ms);	// in case something else follows
				_mc.getPcPort().takeTx(out);
				return out;
			}
		}
		return out;
	}

	std::vector<uint8_t> withChecksum(std::vector<uint8_t> _m)
	{
		uint32_t sum = 0;
		for(const auto b : _m)
			sum += b;
		_m.push_back(static_cast<uint8_t>(sum & 0x7f));
		_m.push_back(0xf7);
		return _m;
	}

	double db(const double _v) { return _v > 0 ? 20.0 * std::log10(_v) : -200.0; }

	// Dominant frequency from zero crossings (good enough to check the note, not for spectra).
	double zeroCrossHz(const std::vector<double>& _x, const double _rate)
	{
		if(_x.size() < 2)
			return 0;
		double mean = 0;
		for(const auto v : _x) mean += v;
		mean /= static_cast<double>(_x.size());
		size_t crossings = 0;
		for(size_t i = 1; i < _x.size(); ++i)
			if((_x[i - 1] - mean) < 0 && (_x[i] - mean) >= 0)
				++crossings;
		return crossings * _rate / static_cast<double>(_x.size());
	}
}

int main(int argc, char** argv)
{
	if(argc < 3)
	{
		std::fprintf(stderr, "usage: g1patchtest ROM patch.pch [--note N] [--seconds S] [--wav file.wav] [--input-sine Hz]\n");
		return 2;
	}
	int note = 60;
	double seconds = 2.0;
	std::string wavPath;
	double inputHz = 0;
	for(int i = 3; i + 1 < argc; i += 2)
	{
		if(!std::strcmp(argv[i], "--note")) note = std::atoi(argv[i + 1]);
		else if(!std::strcmp(argv[i], "--seconds")) seconds = std::atof(argv[i + 1]);
		else if(!std::strcmp(argv[i], "--wav")) wavPath = argv[i + 1];
		else if(!std::strcmp(argv[i], "--input-sine")) inputHz = std::atof(argv[i + 1]);
	}

	// The patch, with NME's module descriptions.
	ModuleDescriptions descs;
	if(!descs.loadFromFile(juce::File(NME_DATA_DIR).getChildFile("modules.xml")))
	{
		std::fprintf(stderr, "cannot load %s/modules.xml\n", NME_DATA_DIR);
		return 1;
	}
	PchFileIO io(descs);
	auto patch = io.readFile(juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2])));
	if(!patch)
	{
		std::fprintf(stderr, "cannot read the patch %s\n", argv[2]);
		return 1;
	}
	PatchSerializer serializer;
	const auto packets = UploadPacketizer::cut(serializer.serializeForUpload(*patch));

	std::ifstream f(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if(rom.size() != g1::g_romSize)
	{
		std::fprintf(stderr, "the ROM must be 512 KB\n");
		return 1;
	}
	g1::Microcontroller mc(rom);
	mc.installRomOsInFlash();

	// Output: one sample per DSP 3 block (4 channels).
	std::vector<std::array<int32_t, 4>> blocks;
	bool capture = false;
	mc.getDsp(g1::g_dspCount - 1).setBlockCallback([&](const int32_t _a, const int32_t _b, const int32_t _c, const int32_t _d)
	{
		if(capture)
			blocks.push_back({_a, _b, _c, _d});
	});

	// --input-sine F: a sine of F Hz (L) and 2F Hz (R) goes into the audio inputs, at -12 dBFS.
	uint64_t inputPhase = 0;
	if(inputHz > 0)
		mc.getDsp(0).setInputProvider([&](int32_t& _l, int32_t& _r)
		{
			const double t = static_cast<double>(inputPhase++) / 96000.0;
			_l = static_cast<int32_t>(0.25 * 8388607.0 * std::sin(2.0 * M_PI * inputHz * t));
			_r = static_cast<int32_t>(0.25 * 8388607.0 * std::sin(2.0 * M_PI * 2.0 * inputHz * t));
		});

	// Boot and handshake.
	run(mc, 1500 * g_ms);
	std::vector<uint8_t> boot;
	mc.getPcPort().takeTx(boot);
	const auto hello = transact(mc, {0xf0, 0x33, 0x00, 0x06, 0x00, 0x03, 0x03, 0xf7}, 1000);
	std::printf("IAm -> %s\n", hex(hello).c_str());
	auto showLcd = [&](const char* _when)
	{
		const auto& lcd = mc.getLcd();
		std::printf("display (%s, %llu writes):\n  |%s|\n  |%s|\n", _when, static_cast<unsigned long long>(lcd.writes()), lcd.line(0, 20).c_str(), lcd.line(1, 20).c_str());
	};
	showLcd("at boot");
	if(hello.empty())
	{
		std::printf("the OS does not answer the handshake\n");
		return 1;
	}

	// What NME sends when it connects, before uploading anything (taken from a recorded session:
	// it asks for the slot state, the patch list, etc.). PID 0 = no patch loaded yet.
	for(const auto& m : std::vector<std::vector<uint8_t>>{
		{0xf0,0x33,0x5c,0x06,0x41,0x14,0x00,0x00}, {0xf0,0x33,0x5c,0x06,0x44,0x02,0x06,0x08,0x04}, {0xf0,0x33,0x5c,0x06,0x41,0x35},
		{0xf0,0x33,0x5c,0x06,0x00,0x20,0x28}, {0xf0,0x33,0x5c,0x06,0x00,0x4b,0x01}, {0xf0,0x33,0x5c,0x06,0x00,0x4b,0x00},
		{0xf0,0x33,0x5c,0x06,0x00,0x53,0x01}, {0xf0,0x33,0x5c,0x06,0x00,0x53,0x00}, {0xf0,0x33,0x5c,0x06,0x00,0x4c,0x01},
		{0xf0,0x33,0x5c,0x06,0x00,0x4c,0x00}, {0xf0,0x33,0x5c,0x06,0x00,0x66}, {0xf0,0x33,0x5c,0x06,0x00,0x63},
		{0xf0,0x33,0x5c,0x06,0x00,0x61}, {0xf0,0x33,0x5c,0x06,0x00,0x4e,0x01}, {0xf0,0x33,0x5c,0x06,0x00,0x4e,0x00},
		{0xf0,0x33,0x5c,0x06,0x00,0x68}})
	{
		const auto reply = transact(mc, withChecksum(m), 200);
		if(std::getenv("G1_VERBOSE"))
			std::printf("  init %s -> %s\n", hex(withChecksum(m), 12).c_str(), hex(reply, 16).c_str());
	}

	// Upload, like NME: a packet, its reply, the next one.
	int pid = -1;
	// How long to wait for each packet's reply. The OS takes much longer at some points of a
	// big patch (it is loading code into the DSPs), so G1_ACKMS raises it.
	const uint32_t ackMs = std::getenv("G1_ACKMS") ? static_cast<uint32_t>(std::atoi(std::getenv("G1_ACKMS"))) : 300;
	for(size_t i = 0; i < packets.size(); ++i)
	{
		const auto msg = UploadPacketizer::frame(packets[i], i == 0, i + 1 == packets.size(), 0);
		const auto before = mc.ucCycles();
		const auto reply = transact(mc, msg, ackMs);
		const auto ms = (mc.ucCycles() - before) / g_ms;
		if(ms > 200 && std::getenv("G1_VERBOSE"))
			std::printf("  packet %zu took %llu ms\n", i + 1, static_cast<unsigned long long>(ms));
		if(std::getenv("G1_VERBOSE"))
			std::printf("  packet %zu: %s -> %s\n", i + 1, hex(msg, 12).c_str(), hex(reply, 16).c_str());
		// Slot 0 ACK: F0 33 58 06 xx 36 pid ...
		for(size_t k = 0; k + 6 < reply.size(); ++k)
			if(reply[k] == 0xf0 && reply[k + 1] == 0x33 && (reply[k + 2] >> 2) == 0x16 && reply[k + 5] == 0x36)
				pid = reply[k + 6];
		if(reply.empty())
			std::printf("  packet %zu/%zu: NO REPLY\n", i + 1, packets.size());
	}
	std::printf("patch \"%s\" uploaded in %zu packets; pid=%d\n", patch->getName().toRawUTF8(), packets.size(), pid);
	if(pid < 0)
	{
		std::printf("the OS has not confirmed the patch\n");
		return 1;
	}
	run(mc, 300 * g_ms);	// let the OS load the DSPs

	// Knob assignments, as NME does after uploading: those in the .pch and, with
	// G1_KNOBS="knob:module:param,...", others (knob 0-17 = 1-18, poly section).
	std::vector<std::array<int, 4>> knobs;	// knob, section, module, parameter
	for(int k = 0; k < 23; ++k)
		if(patch->knobAssignments[static_cast<size_t>(k)].assigned)
		{
			const auto& ka = patch->knobAssignments[static_cast<size_t>(k)];
			knobs.push_back({k, ka.section, ka.module, ka.param});
		}
	if(const char* ks = std::getenv("G1_KNOBS"))
		for(const auto& t : juce::StringArray::fromTokens(ks, ",", ""))
		{
			const auto f = juce::StringArray::fromTokens(t, ":", "");
			if(f.size() == 3)
				knobs.push_back({f[0].getIntValue(), 1, f[1].getIntValue(), f[2].getIntValue()});
		}
	for(const auto& k : knobs)
	{
		const auto reply = transact(mc, KnobAssignmentMessage::assign(pid, k[0], k[1], k[2], k[3], 0), 200);
		if(std::getenv("G1_VERBOSE"))
			std::printf("  knob %d -> module %d param %d: %s\n", k[0], k[2], k[3], hex(reply, 16).c_str());
	}
	if(!knobs.empty())
		run(mc, 200 * g_ms);

	for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		mc.getDsp(d).resetLinkPeak();
	// G1_PCWATCH=174,194: how many times each DSP goes through those addresses during the note
	// (only counted at the start of a JIT block: fine for loops and branches).
	std::vector<uint32_t> watch;
	if(const char* w = std::getenv("G1_PCWATCH"))
		for(const auto& t : juce::StringArray::fromTokens(w, ",", ""))
			watch.push_back(static_cast<uint32_t>(t.getHexValue32()));
	for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		for(const auto a : watch)
			mc.getDsp(d).pcWatch()[a] = 0;
	// The panel before the note. G1_HOLD=row.bit holds a button down (a modifier such as Shift)
	// while the rest are pressed; G1_PREPRESS=row.bit,... presses them before playing, so that
	// what they change can be heard (Oct down/up change the octave of the note).
	const char* hold = std::getenv("G1_HOLD");
	auto button = [&](const juce::String& _rowBit, const bool _down)
	{
		const auto g = juce::StringArray::fromTokens(_rowBit, ".", "");
		mc.setButton(static_cast<uint32_t>(g[0].getIntValue()), static_cast<uint32_t>(g[1].getIntValue()), _down);
	};
	// A step of a sequence can also be a knob or the dial, so that a whole panel gesture can be
	// written down: "1.2,1.6,k1=200,d3,2.6" is Edit, down, knob 1 to 200, three detents, Assign.
	auto gesture = [&](const juce::String& _step) -> bool
	{
		if(_step.startsWithChar('k'))
		{
			static constexpr std::array<uint8_t, 18> adc = {0x31, 0x37, 0x2d, 0x32, 0x28, 0x2e, 0x33, 0x29, 0x2f, 0x34, 0x2a, 0x1a, 0x35, 0x2b, 0x1b, 0x36, 0x2c, 0x1c};
			const auto f = juce::StringArray::fromTokens(_step.substring(1), "=", "");
			const auto knob = f[0].getIntValue();
			if(knob >= 1 && knob <= 18)
				mc.setAdc(adc[static_cast<size_t>(knob - 1)], static_cast<uint8_t>(f[1].getIntValue()));
			return true;
		}
		if(_step.startsWithChar('d'))
		{
			mc.turnDial(_step.substring(1).getIntValue());
			return true;
		}
		return false;
	};
	auto holdButton = [&](const bool _down) { if(hold) { button(hold, _down); run(mc, 50 * g_ms); } };
	auto pressButtons = [&](const juce::StringArray& _list, const int _count)
	{
		for(int k = 0; k < _count; ++k)
		{
			if(gesture(_list[k]))
			{
				run(mc, 300 * g_ms);
				continue;
			}
			button(_list[k], true);
			run(mc, 150 * g_ms);
			button(_list[k], false);
			run(mc, 300 * g_ms);
		}
	};
	// What the OS says to the editor during a gesture: it is what tells an assignment from a
	// plain parameter change.
	auto pcPortSince = [&]
	{
		std::vector<uint8_t> out;
		mc.getPcPort().takeTx(out);
		return out;
	};
	if(const char* pp = std::getenv("G1_PREPRESS"))
	{
		const auto seq = juce::StringArray::fromTokens(pp, ",", "");
		holdButton(true);
		pressButtons(seq, seq.size());
		holdButton(false);
		std::printf("PREPRESS %s%s  display [%s|%s]\n", pp, hold ? (juce::String(" holding ") + hold).toRawUTF8() : "",
			mc.getLcd().line(0, 16).c_str(), mc.getLcd().line(1, 16).c_str());
	}
	blocks.clear();
	capture = true;
	// Note through the PC Port, like NME: cc $17, 56 00 note (press) ... 56 01 note (release).
	// G1_MIDINOTE=channel (1-16) plays it through the MIDI IN port instead, which is not the
	// same road: the editor's note goes straight to the slot, MIDI IN goes through the OS's
	// keyboard handling (channels, octave shift, and so on).
	if(const char* mn = std::getenv("G1_MIDINOTE"))
	{
		const auto ch = static_cast<uint8_t>((std::atoi(mn) - 1) & 0x0f);
		mc.getSci().write({static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(note), 100});
	}
	else
	{
		const auto on = withChecksum({0xf0, 0x33, 0x5c, 0x06, static_cast<uint8_t>(pid), 0x56, 0x00, static_cast<uint8_t>(note)});
		mc.getPcPort().receive(on);
	}
	run(mc, static_cast<uint64_t>(seconds * 1000) * g_ms);
	capture = false;
	std::vector<uint8_t> rest;
	mc.getPcPort().takeTx(rest);
	// $7F is the plain ACK the OS sends for each packet (NME treats it as one), so it is not
	// worth reporting; what matters is whether the patch ended up loaded and sounding.
	if(std::getenv("G1_VERBOSE") && !rest.empty())
		std::printf("the OS said after the note: %s\n", hex(rest, 40).c_str());

	if(std::getenv("G1_VERBOSE"))
		for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		{
			auto& p = mc.getDsp(d).periph();
			for(uint32_t ch = 2; ch < 4; ++ch)
				std::printf("DSP%u  DMA%u DCR=%06x DSR=%06x DDR=%06x DCO=%06x\n", d, ch, p.getDMA().getDCR(ch), p.getDMA().getDSR(ch), p.getDMA().getDDR(ch), p.getDMA().getDCO(ch));
			std::printf("DSP%u  ESSI0 SR=%06x CRB=%06x RX=%06x | ESSI1 SR=%06x CRB=%06x RX=%06x\n", d,
				static_cast<uint32_t>(p.getEssi0().getSR()), static_cast<uint32_t>(p.getEssi0().getCRB()), 0u,
				static_cast<uint32_t>(p.getEssi1().getSR()), static_cast<uint32_t>(p.getEssi1().getCRB()), 0u);
			std::printf("DSP%u  DOR0=%06x DOR1=%06x DCO1=%06x (as read by the program: X:$FFFFF3/F2/E9 = %06x %06x %06x)\n", d,
				p.getDMA().getDOR(0), p.getDMA().getDOR(1), p.getDMA().getDCO(1),
				p.read(0xfffff3, dsp56k::Instruction::Invalid), p.read(0xfffff2, dsp56k::Instruction::Invalid), p.read(0xffffe9, dsp56k::Instruction::Invalid));
		}
	for(const auto a : watch)
	{
		std::printf("PC $%04x:", a);
		for(uint32_t d = 0; d < g1::g_dspCount; ++d)
			std::printf("  DSP%u %llu", d, static_cast<unsigned long long>(mc.getDsp(d).pcWatch()[a]));
		std::printf("\n");
	}
	showLcd("with the note");
	auto leds = [&]
	{
		std::string r;
		for(uint32_t row = 0; row < 4; ++row)
		{
			char b[8];
			std::snprintf(b, sizeof(b), "%02x ", mc.ledRow(row));
			r += b;
		}
		return r;
	};
	std::printf("LEDs (rows 0-3): %s\n", leds().c_str());
	// State of the 32 LEDs, looking 20 times in 1 s: '#' on, '.' off, '*' blinking.
	// Row 0 to 3, bit 7 to 0 (on = bit at 0).
	auto ledStates = [&]
	{
		std::array<int, 32> on{};
		for(int k = 0; k < 20; ++k)
		{
			for(uint32_t i = 0; i < 32; ++i)
				if(!(mc.ledRow(i / 8) & (1u << (i % 8))))
					++on[i];
			run(mc, 50 * g_ms);
		}
		std::string r;
		for(uint32_t row = 0; row < 4; ++row)
		{
			for(int bit = 7; bit >= 0; --bit)
			{
				const int n = on[row * 8 + static_cast<uint32_t>(bit)];
				r += n == 0 ? '.' : (n == 20 ? '#' : '*');
			}
			if(row < 3) r += ' ';
		}
		return r;
	};
	if(std::getenv("G1_LEDSTATE"))
		std::printf("LEDs (row 0..3, bit 7..0): %s\n", ledStates().c_str());
	// G1_PRESS=row.bit: presses that button from the boot state and reports the display and the
	// LEDs before and after. To identify buttons, one per process.
	if(const char* pr = std::getenv("G1_PRESS"))
	{
		// Several, comma-separated: they are pressed in order and the last one is reported.
		const auto seq = juce::StringArray::fromTokens(pr, ",", "");
		pcPortSince();		// drain what came before, so only the gesture's traffic is left
		holdButton(true);
		pressButtons(seq, seq.size() - 1);
		const auto f = juce::StringArray::fromTokens(seq[seq.size() - 1], ".", "");
		const auto row = static_cast<uint32_t>(f[0].getIntValue()), bit = static_cast<uint32_t>(f[1].getIntValue());
		auto screen = [&] { std::string a = mc.getLcd().line(0, 16), b = mc.getLcd().line(1, 16);
			for(auto* s : {&a, &b}) for(auto& c : *s) if(static_cast<uint8_t>(c) < 16) c = '~';
			return "[" + a + "|" + b + "]"; };
		const auto s0 = screen();
		const auto l0 = ledStates();
		mc.setButton(row, bit, true);
		run(mc, 150 * g_ms);
		mc.setButton(row, bit, false);
		run(mc, 300 * g_ms);
		const auto s1 = screen();		// still holding, if anything is held
		const auto said = pcPortSince();
		if(!said.empty())
			std::printf("the OS said: %s\n", hex(said, 40).c_str());
		if(!std::getenv("G1_HOLD_END"))	// with it, the hold lasts until the probes are over
			holdButton(false);
		run(mc, 300 * g_ms);
		std::printf("PRESS %s%s  display %s -> %s -> %s  LEDs %s -> %s\n", pr, hold ? (juce::String(" holding ") + hold).toRawUTF8() : "",
			s0.c_str(), s1.c_str(), screen().c_str(), l0.c_str(), ledStates().c_str());
	}
	// G1_DIAL=n: turns the dial n detents (negative the other way) and reports the display.
	// With G1_PRESS it turns after the presses, so the screen can be set up first.
	if(const char* dl = std::getenv("G1_DIAL"))
	{
		const auto detents = std::atoi(dl);
		mc.turnDial(detents);
		run(mc, static_cast<uint64_t>(std::abs(detents) * 10 + 300) * g_ms);
		std::printf("DIAL %+d  display [%s|%s]\n", detents, mc.getLcd().line(0, 16).c_str(), mc.getLcd().line(1, 16).c_str());
	}
	// G1_ADCSWEEP=1: raises each ADC channel from 0 to 200 and reports what the OS sends on the PC
	// Port (with knobs assigned, a Parameter with the module and value: that tells which knob it is).
	if(std::getenv("G1_ADCSWEEP"))
		for(const uint8_t code : {0x31, 0x37, 0x2d, 0x32, 0x28, 0x2e, 0x33, 0x29, 0x2f, 0x34, 0x2a, 0x1a, 0x35, 0x2b, 0x1b, 0x36, 0x2c, 0x1c, 0x30, 0x18})
		{
			std::vector<uint8_t> drain;
			mc.getPcPort().takeTx(drain);
			mc.setAdc(code, 200);
			run(mc, 250 * g_ms);
			std::vector<uint8_t> outMsgs;
			mc.getPcPort().takeTx(outMsgs);
			std::printf("ADC $%02x ->", code);
			for(size_t k = 0; k + 9 < outMsgs.size(); ++k)
				if(outMsgs[k] == 0xf0 && (outMsgs[k + 2] >> 2) == 0x13 && outMsgs[k + 5] == 0x40)
					std::printf(" [section %u module %u param %u = %u]", outMsgs[k + 6], outMsgs[k + 7], outMsgs[k + 8], outMsgs[k + 9]);
			std::printf("  %s\n", hex(outMsgs, 30).c_str());
			mc.setAdc(code, 0);
			run(mc, 100 * g_ms);
		}
	// G1_PROBE=1: presses the 24 matrix buttons one by one and reports what changes on the display
	// and the LEDs. Useful to find out which panel button each bit is.
	if(std::getenv("G1_PROBE"))
		for(uint32_t row = 0; row < 3; ++row)
			for(uint32_t bit = 0; bit < 8; ++bit)
			{
				const auto l0 = mc.getLcd().line(0, 20) + "|" + mc.getLcd().line(1, 20);
				const auto led0 = leds();
				mc.setButton(row, bit, true);
				run(mc, 120 * g_ms);
				mc.setButton(row, bit, false);
				run(mc, 400 * g_ms);
				const auto l1 = mc.getLcd().line(0, 20) + "|" + mc.getLcd().line(1, 20);
				const auto led1 = leds();
				std::printf("button row %u bit %u: %s%s%s%s\n", row, bit,
					l1 != l0 ? ("display [" + l1 + "]") .c_str() : "", led1 != led0 ? (" LEDs " + led0 + "-> " + led1).c_str() : "",
					(l1 == l0 && led1 == led0) ? "no change" : "", "");
			}
	// G1_PEEK=addr[,addr...]: bytes of the CPU's memory, to see the OS's own variables
	// (for instance $1C3AB8 + slot, the octave shift of each slot).
	if(const char* pk = std::getenv("G1_PEEK"))
	{
		std::printf("PEEK");
		for(const auto& t : juce::StringArray::fromTokens(pk, ",", ""))
		{
			const auto a = static_cast<uint32_t>(t.getHexValue32());
			std::printf("  $%06x=$%02x", a, mc.read8(a));
		}
		std::printf("\n");
	}
	if(std::getenv("G1_HOLD_END"))
	{
		holdButton(false);
		run(mc, 300 * g_ms);
		std::printf("released  display [%s|%s]\n", mc.getLcd().line(0, 16).c_str(), mc.getLcd().line(1, 16).c_str());
	}
	// Report: the four outputs (without the $155 silence) and the links.
	std::printf("\noutputs (%.2f s, %zu samples at 96 kHz):\n", blocks.size() / 96000.0, blocks.size());
	for(uint32_t c = 0; c < 4; ++c)
	{
		std::vector<double> x;
		x.reserve(blocks.size());
		double peak = 0;
		for(const auto& b : blocks)
		{
			const double v = (b[c] - g_silence) / 8388608.0;
			x.push_back(v);
			peak = std::max(peak, std::fabs(v));
		}
		if(peak == 0)
		{
			std::printf("  output %u: silence\n", c + 1);
			continue;
		}
		// Slow signals (LFOs, envelopes, sequencers) barely move the peak, so the capture is
		// cut into 20 slices: the mean says where the signal sits and the drift, how far the
		// slice means travel, says whether it is moving at all.
		double mean = 0;
		for(const auto v : x) mean += v;
		mean /= static_cast<double>(x.size());
		double lo = 1e9, hi = -1e9;
		constexpr size_t slices = 20;
		for(size_t s2 = 0; s2 < slices; ++s2)
		{
			const auto from = x.size() * s2 / slices, to = x.size() * (s2 + 1) / slices;
			double m = 0;
			for(auto i = from; i < to; ++i) m += x[i];
			m /= static_cast<double>(to - from ? to - from : 1);
			lo = std::min(lo, m);
			hi = std::max(hi, m);
		}
		std::printf("  output %u: peak %6.1f dBFS (%+.1f with g1run's +36 dB), ~%.1f Hz, mean %+.4f, drift %.4f\n",
			c + 1, db(peak), db(peak) + 36, zeroCrossHz(x, 96000), mean, hi - lo);
	}
	std::printf("links (peak per channel, dBFS; '.' = zero):\n");
	for(uint32_t d = 0; d + 1 < g1::g_dspCount; ++d)
	{
		std::printf("  DSP%u -> DSP%u:", d, d + 1);
		const auto& p = mc.getDsp(d).linkPeak();
		for(size_t i = 0; i < p.size(); ++i)
		{
			if(i == 9) std::printf(" |");
			if(p[i]) std::printf(" %4.0f", db(p[i] / 8388608.0));
			else std::printf("    .");
		}
		std::printf("\n");
	}

	// G1_DUMP=dir: P, X and Y memory of each DSP at the end (to disassemble with dspdis).
	if(const char* dir = std::getenv("G1_DUMP"))
		for(uint32_t d = 0; d < g1::g_dspCount; ++d)
			for(const auto& [area, name] : std::vector<std::pair<dsp56k::EMemArea, const char*>>{{dsp56k::MemArea_P, "p"}, {dsp56k::MemArea_X, "x"}, {dsp56k::MemArea_Y, "y"}})
				if(FILE* fp = std::fopen((std::string(dir) + "/dsp" + std::to_string(d) + "_" + name + ".hex").c_str(), "w"))
				{
					auto& mem = mc.getDsp(d).dsp().memory();
					for(dsp56k::TWord a = 0; a < 0x1000; ++a)
						std::fprintf(fp, "%06x\n", mem.get(area, a));
					std::fclose(fp);
				}

	if(!wavPath.empty())
	{
		std::ofstream w(wavPath, std::ios::binary);
		const uint32_t rate = 96000, ch = 4, bytes = 3, n = static_cast<uint32_t>(blocks.size());
		auto u32 = [&](const uint32_t v) { w.write(reinterpret_cast<const char*>(&v), 4); };
		auto u16 = [&](const uint16_t v) { w.write(reinterpret_cast<const char*>(&v), 2); };
		w.write("RIFF", 4); u32(36 + n * ch * bytes); w.write("WAVEfmt ", 8); u32(16); u16(1); u16(ch);
		u32(rate); u32(rate * ch * bytes); u16(ch * bytes); u16(24); w.write("data", 4); u32(n * ch * bytes);
		const double gain = std::pow(10.0, 36.0 / 20.0);
		for(const auto& b : blocks)
			for(uint32_t c = 0; c < ch; ++c)
			{
				const auto v = static_cast<int32_t>(std::clamp((b[c] - g_silence) * gain, -8388608.0, 8388607.0));
				const char s[3] = {static_cast<char>(v & 0xff), static_cast<char>((v >> 8) & 0xff), static_cast<char>((v >> 16) & 0xff)};
				w.write(s, 3);
			}
		std::printf("WAV (4 channels, 96 kHz, +36 dB) in %s\n", wavPath.c_str());
	}
	return 0;
}
