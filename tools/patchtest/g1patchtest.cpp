// g1patchtest: test bench for the emulated G1, with no window and no editor.
//
//   g1patchtest ROM patch.pch [--note 60] [--seconds 2] [--wav output.wav] [--dump-packets dir] [--bench]
//   (--note -1 plays no note: what is measured is what the patch does by itself)
//   (--note-at S plays the note S seconds into the measurement: before it, the patch alone)
//   (--note-off-at S releases it S seconds in, after --note-at: an envelope's release, measured)
//   (--events +0.1,-0.3,+0.35 presses (+) and releases (-) the note at those seconds into the
//    measurement, in order: retriggers and short gates, all in one recording)
//   (--input-raw file.f32 feeds mono float32 samples (1.0 = full scale, 96 kHz) into both audio
//    inputs from the first measured sample on: impulses, sweeps, noise, for measuring filters)
//   (G1_CHORD / G1_SEQ: press and release notes through the editor's port, then measure)
//   (G1_REALTIME=seconds: play in real time against a simulated sound card, counting dropouts)
//
// Boots the OS, greets like NME (IAm), uploads the patch with the same code NME uses
// (PchFileIO -> PatchSerializer -> UploadPacketizer), packet by packet waiting for each
// reply, plays a note through the PC Port and measures the four outputs and the DSP links.
// Useful to test module by module what sounds and what does not, without touching anyone's G1.
#include "g1Lib/g1mc.h"
#include "g1Lib/g1dsp.h"
#include "g1Lib/g1knobs.h"

#include "model/ModuleDescriptions.h"
#include "protocol/NewModuleMessage.h"
#include "model/Patch.h"
#include "model/PatchSerializer.h"
#include "model/PchFileIO.h"
#include "midi/UploadPacketizer.h"
#include "protocol/KnobAssignmentMessage.h"
#include "midi/NmMessages.h"
#include "midi/SysExCodec.h"

#include "../../app/audiobridge.h"

#include <algorithm>
#include <map>
#include <atomic>
#include <thread>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

// Everything the G1 sends on the PC Port is collected here before the tool reads or drops it;
// G1_PCPORT_LOG=file also appends it to that file, raw (the module lights, meters and replies a
// test never looks at).
static void takeTx(g1::Microcontroller& _mc, std::vector<uint8_t>& _out)
{
	_mc.getPcPort().takeTx(_out);
	static FILE* log = std::getenv("G1_PCPORT_LOG") ? std::fopen(std::getenv("G1_PCPORT_LOG"), "wb") : nullptr;
	if(log && !_out.empty())
	{
		std::fwrite(_out.data(), 1, _out.size(), log);
		std::fflush(log);
	}
}


namespace
{
	constexpr uint64_t g_ms = g1::g_ucClock / 1000;
	constexpr int32_t g_silence = 0x155;	// DSP 3's X:$5F: what comes out with no signal

	// G1_WATCH_PC=addr (hex): each time the 68k reaches that address, its registers and the top of
	// its stack (the routine's arguments, when it is a routine's first instruction), the first 8 times.
	void watchPc(g1::Microcontroller& _mc)
	{
		static const uint32_t at = std::getenv("G1_WATCH_PC") ? static_cast<uint32_t>(std::strtoul(std::getenv("G1_WATCH_PC"), nullptr, 16)) : 0;
		static int hits = 0;
		// G1_WATCH_FROM=S: only from S seconds of the G1's time on (boot included)
		static const uint64_t from = std::getenv("G1_WATCH_FROM") ? static_cast<uint64_t>(std::atof(std::getenv("G1_WATCH_FROM")) * g1::g_ucClock) : 0;
		if(!at || _mc.getPC() != at || hits >= 8 || _mc.ucCycles() < from)
			return;
		++hits;
		std::printf("  [watch $%06x] #%d D0=%08x D1=%08x D2=%08x A0=%06x A1=%06x A2=%06x SP=%06x stack:", at, hits,
			_mc.getDReg(0), _mc.getDReg(1), _mc.getDReg(2), _mc.getAReg(0), _mc.getAReg(1), _mc.getAReg(2), _mc.getAReg(7));
		for(uint32_t i = 0; i < 24; i += 2)
			std::printf(" %04x", _mc.read16(_mc.getAReg(7) + i));
		std::printf("\n");
	}

	void run(g1::Microcontroller& _mc, const uint64_t _ucCycles)
	{
		const auto end = _mc.ucCycles() + _ucCycles;
		const bool watching = std::getenv("G1_WATCH_PC") != nullptr;
		while(_mc.ucCycles() < end)
		{
			_mc.exec();
			if(watching)
				watchPc(_mc);
		}
		_mc.syncDsps();	// what the DSPs produced up to here is delivered before it is measured
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
			takeTx(_mc, out);
			if(!out.empty() && out.back() == 0xf7)
			{
				run(_mc, 5 * g_ms);	// in case something else follows
				takeTx(_mc, out);
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
		std::fprintf(stderr, "usage: g1patchtest ROM patch.pch [--note N|-1] [--note-at S] [--note-off-at S] [--events +S,-S,...] [--param-at S:section:module:param:value] [--add-module-at S:type:index:x:y] [--knob-at S:knob] [--seconds S] [--wav file.wav] [--input-sine Hz] [--input-raw file.f32] [--dump-packets dir] [--bench]\n");
		std::fprintf(stderr, "cycle diagnostic: G1_JITBLOCK=1 G1_CYCPROF=0..3 (after the note, with disassembly)\n");
		return 2;
	}
	int note = 60;
	double seconds = 2.0;
	double noteAt = -1.0;	// --note-at S: play the note S seconds into the measurement
	double noteOffAt = -1.0;	// --note-off-at S: release it S seconds in (needs --note-at)
	std::vector<std::pair<double, bool>> events;	// --events: (seconds, press) in time order
	// --param-at S:section:module:param:value (repeatable): a parameter change at S seconds, the
	// message NME sends when a knob is turned (cc $13, sc $40)
	struct ParamAt { double at; int section, module, param, value; };
	std::vector<ParamAt> paramsAt;
	// --knob-at S:knob (repeatable): panel knob 1-18 swept from 0 to the top over 0.4 s, from S
	// seconds in, as a hand on the panel would (the ADC, not the editor's message)
	struct KnobAt { double at; int knob; };
	std::vector<KnobAt> knobsAt;
	// --add-module-at S:type:index:x:y (repeatable): a module added to the poly section S seconds
	// in, with its default parameters, the message NME sends when one is dropped on the canvas
	struct AddAt { double at; int type, index, x, y; };
	std::vector<AddAt> addsAt;
	std::string wavPath;
	double inputHz = 0;
	std::vector<float> inputRaw;	// --input-raw: fed from the first captured sample
	std::string dumpDir;
	bool bench = std::getenv("G1_BENCH") != nullptr;
	int cycleProfile = -1;
	if(const char* cp = std::getenv("G1_CYCPROF"))
	{
		const char* jb = std::getenv("G1_JITBLOCK");
		const char* interp = std::getenv("G1_INTERP");
		if(cp[0] < '0' || cp[0] > '3' || cp[1] || !jb || std::strcmp(jb, "1")
			|| (interp && (std::atoi(interp) & (1 << (cp[0] - '0')))))
		{
			std::fprintf(stderr, "G1_CYCPROF requires a DSP index 0..3, G1_JITBLOCK=1 and JIT on that DSP\n");
			return 2;
		}
		cycleProfile = cp[0] - '0';
	}
	for(int i = 3; i < argc; )
	{
		if(!std::strcmp(argv[i], "--bench")) { bench = true; ++i; continue; }
		if(i + 1 >= argc) break;
		if(!std::strcmp(argv[i], "--note")) note = std::atoi(argv[i + 1]);
		else if(!std::strcmp(argv[i], "--seconds")) seconds = std::atof(argv[i + 1]);
		else if(!std::strcmp(argv[i], "--note-at")) noteAt = std::atof(argv[i + 1]);
		else if(!std::strcmp(argv[i], "--note-off-at")) noteOffAt = std::atof(argv[i + 1]);
		else if(!std::strcmp(argv[i], "--param-at"))
		{
			ParamAt pa{};
			if(std::sscanf(argv[i + 1], "%lf:%d:%d:%d:%d", &pa.at, &pa.section, &pa.module, &pa.param, &pa.value) == 5)
				paramsAt.push_back(pa);
		}
		else if(!std::strcmp(argv[i], "--knob-at"))
		{
			KnobAt ka{};
			if(std::sscanf(argv[i + 1], "%lf:%d", &ka.at, &ka.knob) == 2 && ka.knob >= 1 && ka.knob <= 18)
				knobsAt.push_back(ka);
		}
		else if(!std::strcmp(argv[i], "--add-module-at"))
		{
			AddAt aa{};
			if(std::sscanf(argv[i + 1], "%lf:%d:%d:%d:%d", &aa.at, &aa.type, &aa.index, &aa.x, &aa.y) == 5)
				addsAt.push_back(aa);
		}
		else if(!std::strcmp(argv[i], "--events"))
		{
			for(const char* c = argv[i + 1]; *c; )
			{
				events.emplace_back(std::atof(c + 1), *c == '+');
				while(*c && *c != ',') ++c;
				if(*c == ',') ++c;
			}
		}
		else if(!std::strcmp(argv[i], "--wav")) wavPath = argv[i + 1];
		else if(!std::strcmp(argv[i], "--input-sine")) inputHz = std::atof(argv[i + 1]);
		else if(!std::strcmp(argv[i], "--input-raw"))
		{
			std::ifstream f(argv[i + 1], std::ios::binary);
			f.seekg(0, std::ios::end);
			inputRaw.resize(static_cast<size_t>(f.tellg()) / sizeof(float));
			f.seekg(0);
			f.read(reinterpret_cast<char*>(inputRaw.data()), static_cast<std::streamsize>(inputRaw.size() * sizeof(float)));
		}
		else if(!std::strcmp(argv[i], "--dump-packets")) dumpDir = argv[i + 1];
		i += 2;
	}

	// The patch, with NME's module descriptions (G1_MODULES_XML=file: another descriptions file,
	// e.g. one describing module types an experimental OS adds).
	ModuleDescriptions descs;
	const auto descsFile = std::getenv("G1_MODULES_XML")
		? juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(std::getenv("G1_MODULES_XML")))
		: juce::File(NME_DATA_DIR).getChildFile("modules.xml");
	if(!descs.loadFromFile(descsFile))
	{
		std::fprintf(stderr, "cannot load %s\n", descsFile.getFullPathName().toRawUTF8());
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
	// G1_MODEL=expanded: the 8 DSPs of the expansion board.
	const char* model = std::getenv("G1_MODEL");
	g1::Microcontroller mc(rom, model && std::string(model) == "expanded" ? g1::g_expandedDsps : g1::g_regularDsps);
	mc.setBenchEnabled(bench);
	// G1_OS=image: run that OS (Clavia's 3.03b update) instead of the ROM's factory one.
	bool osImage = false;
	if(const char* osPath = std::getenv("G1_OS"))
	{
		std::ifstream of(osPath, std::ios::binary);
		const std::vector<uint8_t> os((std::istreambuf_iterator<char>(of)), std::istreambuf_iterator<char>());
		osImage = mc.installOsInFlash(os);
		std::printf("OS image %s: %s\n", osPath, osImage ? "installed" : "NOT an OS image, the ROM's is used");
	}
	if(!osImage)
		mc.installRomOsInFlash();

	// Output: one sample per DSP 3 block (4 channels).
	std::vector<std::array<int32_t, 4>> blocks;
	bool capture = false;
	mc.outputDsp().setBlockCallback([&](const int32_t _a, const int32_t _b, const int32_t _c, const int32_t _d)
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
			constexpr double twoPi = 6.28318530717958647692;
			_l = static_cast<int32_t>(0.25 * 8388607.0 * std::sin(twoPi * inputHz * t));
			_r = static_cast<int32_t>(0.25 * 8388607.0 * std::sin(twoPi * 2.0 * inputHz * t));
		});

	// --input-raw: silence until the measurement starts, then the file sample by sample, so input
	// sample i and captured output frame i line up (up to the patch's own fixed latency).
	size_t inputPos = 0;
	if(!inputRaw.empty())
		mc.getDsp(0).setInputProvider([&](int32_t& _l, int32_t& _r)
		{
			float v = 0.0f;
			if(capture && inputPos < inputRaw.size())
				v = inputRaw[inputPos++];
			const auto x = static_cast<int32_t>(std::max(-1.0f, std::min(1.0f, v)) * 8388607.0f);
			_l = x;
			_r = x;
		});

	// Boot and handshake.
	// G1_BOOTMS: longer for a first boot of an OS that formats its patch storage (INIT FLASH).
	run(mc, static_cast<uint64_t>(std::getenv("G1_BOOTMS") ? std::atoi(std::getenv("G1_BOOTMS")) : (osImage ? 6000 : 1500)) * g_ms);
	std::vector<uint8_t> boot;
	takeTx(mc, boot);
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

	// G1_CLOCKSRC=0|1: read the synth settings, set the MIDI clock source to that value and send
	// them back, as NME's Synth Settings dialog does. 1 runs the internal master clock (what
	// MIDIGlobal and every clocked patch follow); 0, the default of a new flash, waits for MIDI
	// clock. See NOTES.md, "The master clock".
	if(const char* cs = std::getenv("G1_CLOCKSRC"))
	{
		const auto reply = transact(mc, withChecksum({0xf0, 0x33, 0x5c, 0x06, 0x44, 0x02, 0x06, 0x08, 0x04}), 500);
		SynthSettings st;
		bool ok = false;
		for(size_t k = 0; k + 4 < reply.size() && !ok; ++k)
		{
			if(reply[k] != 0xf0 || reply[k + 1] != 0x33) continue;
			const int cc = reply[k + 2] >> 2;
			if(cc < 0x1c) continue;
			size_t e = k + 4;
			while(e < reply.size() && reply[e] != 0xf7) ++e;
			const auto pkt = PatchPacketMessage::decode(cc, reply.data() + k + 4, e - (k + 4));
			ok = SynthSettingsMessage::decode(pkt.patchData, st);
		}
		std::printf("synth settings read: %s, clock source was %d, bpm %d\n", ok ? "yes" : "NO", st.midiClockSource, st.midiClockBpm);
		if(ok)
		{
			st.midiClockSource = std::atoi(cs);
			SynthSettingsMessage m;
			m.settings = st;
			mc.getPcPort().receive(SysEx::encode(0x1f, 0, m.encode(0), true));
			run(mc, 1000 * g_ms);
			std::vector<uint8_t> drop;
			takeTx(mc, drop);
			std::printf("clock source set to %d\n", st.midiClockSource);
		}
	}

	// G1_ABORT_AFTER=N: the main upload stops after N packets, without the last flag, and the OS is
	// asked for its synth settings before and after a closing packet (an empty one with the last
	// flag, NME's closeTransferFrame). G1_CLOSE=good (default), bad (its checksum off by one) or
	// none. Tells whether the OS checks the closing packet's checksum before leaving bulk receive.
	size_t abortAfter = 0;
	if(const char* aa = std::getenv("G1_ABORT_AFTER"))
		abortAfter = static_cast<size_t>(std::atoi(aa));

	// Upload, like NME: a packet, its reply, the next one.
	auto upload = [&](const std::vector<UploadPacketizer::Packet>& packets) -> int
	{
	int pid = -1;
	std::vector<uint8_t> carry;
	// How long to wait for each packet's reply. The OS takes much longer at some points of a
	// big patch (it is loading code into the DSPs), so G1_ACKMS raises it.
	const uint32_t ackMs = std::getenv("G1_ACKMS") ? static_cast<uint32_t>(std::atoi(std::getenv("G1_ACKMS"))) : 300;
	for(size_t i = 0; i < packets.size(); ++i)
	{
		if(abortAfter > 0 && i == abortAfter)
			break;	// G1_ABORT_AFTER: an upload cut short, the last flag never sent
		const auto msg = UploadPacketizer::frame(packets[i], i == 0, i + 1 == packets.size(), 0);
		// --dump-packets: the exact SysEx NME would send, one file per packet, to replay it over
		// a real MIDI port against g1run (docs/upload-timeouts.md).
		if(!dumpDir.empty())
		{
			char name[32];
			std::snprintf(name, sizeof(name), "packet-%02zu.syx", i);
			std::ofstream(dumpDir + "/" + name, std::ios::binary).write(reinterpret_cast<const char*>(msg.data()), static_cast<std::streamsize>(msg.size()));
		}
		const auto before = mc.ucCycles();
		auto reply = transact(mc, msg, ackMs);
		// Like NME, the next packet waits for the ACK ($36 or $7F), not for any reply: after a packet
		// that makes the OS reload the DSPs it first reports the voice count, and it resets the PC
		// Port receiver before it ACKs, which throws away a packet sent too early.
		auto hasAck = [](const std::vector<uint8_t>& _r)
		{
			for(size_t k = 0; k + 5 < _r.size(); ++k)
				if(_r[k] == 0xf0 && _r[k + 1] == 0x33 && (_r[k + 2] >> 2) == 0x16 && (_r[k + 5] == 0x36 || _r[k + 5] == 0x7f))
					return true;
			return false;
		};
		for(uint32_t t = 0; !hasAck(reply) && t < ackMs; ++t)
		{
			run(mc, g_ms);
			std::vector<uint8_t> more;
			takeTx(mc, more);
			reply.insert(reply.end(), more.begin(), more.end());
		}
		const auto ms = (mc.ucCycles() - before) / g_ms;
		if(ms > 200 && std::getenv("G1_VERBOSE"))
			std::printf("  packet %zu took %llu ms\n", i + 1, static_cast<unsigned long long>(ms));
		if(std::getenv("G1_VERBOSE"))
			std::printf("  packet %zu: %s -> %s\n", i + 1, hex(msg, 12).c_str(), hex(reply, 16).c_str());
		// Slot 0 ACK: F0 33 58 06 xx 36 pid ... A reply can straddle two packets' reads, so the
		// tail of the previous one is kept in front of this one.
		carry.insert(carry.end(), reply.begin(), reply.end());
		for(size_t k = 0; k + 6 < carry.size(); ++k)
			if(carry[k] == 0xf0 && carry[k + 1] == 0x33 && (carry[k + 2] >> 2) == 0x16 && carry[k + 5] == 0x36)
				pid = carry[k + 6];
		if(carry.size() > 16)
			carry.erase(carry.begin(), carry.end() - 16);
		if(!hasAck(reply))
			std::printf("  packet %zu/%zu: NO REPLY\n", i + 1, packets.size());
	}
	return pid;
	};

	// G1_BEFORE="a.pch|b.pch": patches uploaded to the same slot first, one after the other with
	// G1_BEFORE_MS of emulated time between them (default 500), as a session in NME does.
	if(const char* before = std::getenv("G1_BEFORE"))
	{
		const auto gapMs = static_cast<uint64_t>(std::getenv("G1_BEFORE_MS") ? std::atoi(std::getenv("G1_BEFORE_MS")) : 500);
		for(const auto& path : juce::StringArray::fromTokens(before, "|", ""))
		{
			auto p = io.readFile(juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(path)));
			if(!p) { std::fprintf(stderr, "cannot read the patch %s\n", path.toRawUTF8()); return 1; }
			const auto pp = UploadPacketizer::cut(serializer.serializeForUpload(*p));
			const int bp = upload(pp);
			std::printf("before: \"%s\" in %zu packets; pid=%d\n", p->getName().toRawUTF8(), pp.size(), bp);
			run(mc, gapMs * g_ms);
			std::vector<uint8_t> drop;
			takeTx(mc, drop);
		}
	}
	int pid = upload(packets);
	if(abortAfter > 0)
	{
		const auto settingsRequest = withChecksum({0xf0, 0x33, 0x5c, 0x06, 0x44, 0x02, 0x06, 0x08, 0x04});
		auto answers = [&]()
		{
			const auto reply = transact(mc, settingsRequest, 1000);
			for(size_t k = 0; k + 2 < reply.size(); ++k)
				if(reply[k] == 0xf0 && reply[k + 1] == 0x33 && (reply[k + 2] >> 2) >= 0x1c)
					return true;
			return false;
		};
		std::printf("upload cut after %zu of %zu packets\n", abortAfter, packets.size());
		std::printf("  settings request before closing: %s\n", answers() ? "answered" : "NO ANSWER");
		const std::string close = std::getenv("G1_CLOSE") ? std::getenv("G1_CLOSE") : "good";
		if(close != "none")
		{
			auto msg = UploadPacketizer::closeTransferFrame(0);
			if(close == "bad")
				msg[msg.size() - 2] = static_cast<uint8_t>((msg[msg.size() - 2] + 1) & 0x7f);
			const auto reply = transact(mc, msg, 1000);
			std::printf("  closing packet (%s checksum): %s -> %s\n", close.c_str(), hex(msg, 16).c_str(), hex(reply, 16).c_str());
		}
		std::printf("  settings request after: %s\n", answers() ? "answered" : "NO ANSWER");
		std::printf("  and once more: %s\n", answers() ? "answered" : "NO ANSWER");
		return 0;
	}
	// G1_PCHIST=ms: where the CPU spends the next ms of emulated time, most visited PCs first
	// (to see which loop the OS sits in after an upload it did not answer).
	if(const char* ph = std::getenv("G1_PCHIST"))
	{
		// ...and where each DSP's blocks start in the same time.
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
			for(uint32_t a = 0; a < 0x1000; ++a)
				mc.getDsp(d).pcWatch()[a] = 0;
		std::map<uint32_t, uint64_t> hits;
		const auto end = mc.ucCycles() + static_cast<uint64_t>(std::atoi(ph)) * g_ms;
		uint64_t total = 0;
		while(mc.ucCycles() < end)
		{
			++hits[mc.getPC()];
			++total;
			mc.exec();
		}
		std::vector<std::pair<uint64_t, uint32_t>> top;
		for(const auto& [pc, n] : hits)
			top.push_back({n, pc});
		std::sort(top.rbegin(), top.rend());
		std::printf("PC histogram, %llu instructions:\n", static_cast<unsigned long long>(total));
		for(size_t i = 0; i < top.size() && i < 16; ++i)
			std::printf("  $%06x %5.1f%%\n", top[i].second, 100.0 * static_cast<double>(top[i].first) / static_cast<double>(total));
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
		{
			std::vector<std::pair<uint64_t, uint32_t>> dt;
			uint64_t dn = 0;
			for(const auto& [pc, n] : mc.getDsp(d).pcWatch())
				if(n) { dt.push_back({n, pc}); dn += n; }
			std::sort(dt.rbegin(), dt.rend());
			std::printf("DSP%u block starts (%llu):", d, static_cast<unsigned long long>(dn));
			for(size_t i = 0; i < dt.size() && i < 20; ++i)
				std::printf(" $%03x:%llu", dt[i].second, static_cast<unsigned long long>(dt[i].first));
			std::printf("\n");
			mc.getDsp(d).pcWatch().clear();
		}
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
	// G1_KNOBINFO=1: what the OS has on each of the 18 panel knobs (g1knobs.h), as the window shows it.
	if(std::getenv("G1_KNOBINFO"))
	{
		g1::KnobMap map(mc);
		for(uint32_t k = 0; k < 18; ++k)
		{
			const auto i = map.read(k);
			if(i.assigned)
				std::printf("KNOB %2u: slot %c section %u module %u (type %u) param %u  \"%s\" / \"%s\"  %u of %u\n", k + 1,
					'A' + i.slot, i.section, i.module, i.type, i.param, i.moduleName.c_str(), i.paramName.c_str(), i.value, i.max);
		}
	}
	// G1_FETCHCHECK=1: asks the OS for the patch back, section by section as NME does, and compares
	// each reply (7-bit unpacked) with the sections NME uploaded (issue #25: can a fetched patch be
	// uploaded again as it came?).
	if(std::getenv("G1_FETCHCHECK"))
	{
		auto unpack7 = [](const std::vector<uint8_t>& _in)
		{
			std::vector<uint8_t> out;
			uint32_t buf = 0;
			int held = 0;
			for(const auto c : _in)
			{
				buf = (buf << 7) | (c & 0x7f);
				held += 7;
				if(held >= 8)
				{
					held -= 8;
					out.push_back(static_cast<uint8_t>((buf >> held) & 0xff));
				}
			}
			return out;
		};
		const std::vector<std::vector<uint8_t>> requests = {
			{0x20, 0x28}, {0x4b, 0x01}, {0x4b, 0x00}, {0x53, 0x01}, {0x53, 0x00}, {0x4c, 0x01}, {0x4c, 0x00},
			{0x66}, {0x63}, {0x61}, {0x4e, 0x01}, {0x4e, 0x00}, {0x68}};
		auto fetch = [&](const int _pid)
		{
			std::vector<std::vector<uint8_t>> fetched;
			for(const auto& rq : requests)
			{
				std::vector<uint8_t> m = {0xf0, 0x33, 0x5c, 0x06, static_cast<uint8_t>(_pid)};
				m.insert(m.end(), rq.begin(), rq.end());
				mc.getPcPort().receive(withChecksum(m));
				// Every message of the reply, until the packet that ends the section (or 2 s).
				std::vector<uint8_t> pending, section;
				bool done = false;
				for(uint32_t t = 0; t < 2000; ++t)
				{
					run(mc, g_ms);
					std::vector<uint8_t> more;
					takeTx(mc, more);
					pending.insert(pending.end(), more.begin(), more.end());
					size_t k = 0;
					for(;;)
					{
						const auto start = std::find(pending.begin() + static_cast<long>(k), pending.end(), uint8_t(0xf0));
						const auto end = std::find(start, pending.end(), uint8_t(0xf7));
						if(start == pending.end() || end == pending.end())
							break;
						const std::vector<uint8_t> msg(start, end + 1);
						k = static_cast<size_t>(end - pending.begin()) + 1;
						if(msg.size() < 6 || msg[1] != 0x33 || (msg[2] >> 2) < 0x1c)
							continue;
						const auto pkt = PatchPacketMessage::decode(msg[2] >> 2, msg.data() + 4, msg.size() - 5);
						if(pkt.isFirst) section.clear();
						section.insert(section.end(), pkt.patchData.begin(), pkt.patchData.end());
						if(pkt.isLast)
						{
							fetched.push_back(unpack7(section));
							section.clear();
							if(!done)
								t = std::max<uint32_t>(t, 1900);	// a second section may follow (CustomDump after NameDump)
							done = true;
						}
					}
					pending.erase(pending.begin(), pending.begin() + static_cast<long>(k));
				}
				if(!done)
					std::printf("  FETCH: no complete reply to %s\n", hex(m, 8).c_str());
				run(mc, 20 * g_ms);
				std::vector<uint8_t> drop;
				takeTx(mc, drop);
			}
			return fetched;
		};
		const auto fetched = fetch(pid);
		const auto sent = serializer.serializeForUpload(*patch);
		std::printf("FETCH: %zu replies for %zu requests; %zu sections were uploaded\n", fetched.size(), requests.size(), sent.size());
		for(size_t i = 0; i < fetched.size(); ++i)
		{
			const auto& f = fetched[i];
			std::printf("  got  type %3d %4zu bytes: %s\n", f.empty() ? -1 : f[0], f.size(), hex(f, 24).c_str());
		}
		for(size_t i = 0; i < sent.size(); ++i)
		{
			const auto& s = sent[i];
			bool match = false;
			for(const auto& f : fetched)
				if(f.size() >= s.size() && std::equal(s.begin(), s.end(), f.begin()))
					match = true;
			std::printf("  sent type %3d %4zu bytes %s: %s\n", s.empty() ? -1 : s[0], s.size(), match ? "== a reply's start" : "NO MATCH", hex(s, 24).c_str());
		}

		// Round trip: the fetched sections in upload order (the header reply carries the header,
		// 11 bytes, then PatchName2), uploaded again over the same slot and fetched once more.
		if(fetched.size() == requests.size())
		{
			const auto& h = fetched[0];
			std::vector<std::vector<uint8_t>> up;
			std::vector<uint8_t> name = {55, 0, 0, 0};	// PatchName (55) from PatchName2 (39)
			name.insert(name.end(), h.begin() + 12, h.end());
			up.push_back(name);
			up.emplace_back(h.begin(), h.begin() + 11);	// Header (33)
			for(const size_t i : {size_t(1), size_t(2), size_t(12), size_t(3), size_t(4), size_t(5), size_t(6), size_t(7), size_t(8), size_t(9)})
				up.push_back(fetched[i]);
			up.push_back({91, 0x80});	// CustomDump poly, empty
			up.push_back({91, 0x00});	// CustomDump common, empty
			up.push_back(fetched[10]);
			up.push_back(fetched[11]);
			for(const auto& u : up)
				std::printf("  up type %3d %3zu: %s\n", u[0], u.size(), hex(u, 20).c_str());
			const int pid2 = upload(UploadPacketizer::cut(up));
			run(mc, 300 * g_ms);
			const auto again = fetch(pid2);
			size_t same = 0;
			for(size_t i = 0; i < again.size() && i < fetched.size(); ++i)
			{
				const bool eq = again[i] == fetched[i];
				same += eq;
				if(!eq)
					std::printf("  ROUNDTRIP section %zu differs:\n    was %s\n    now %s\n", i, hex(fetched[i], 400).c_str(), hex(again[i], 400).c_str());
			}
			std::printf("ROUNDTRIP: pid %d -> %d, %zu of %zu sections identical\n", pid, pid2, same, fetched.size());
			// The same patch, read twice? The last byte of a section is padding (or, in the header,
			// a field the OS bumps on every upload), and the OS may list the cables in another order.
			auto bitsOf = [](const std::vector<uint8_t>& _b)
			{
				std::string bits;
				for(const auto c : _b)
					for(int i = 7; i >= 0; --i)
						bits += ((c >> i) & 1) ? '1' : '0';
				return bits;
			};
			std::vector<size_t> semanticDiff;
			for(size_t i = 0; i < again.size() && i < fetched.size(); ++i)
			{
				const auto& x = fetched[i];
				const auto& y = again[i];
				bool eq = x.size() == y.size();
				if(eq && (i == 3 || i == 4) && x.size() >= 3)	// cables: header, then 30-bit records
				{
					const auto bx = bitsOf(x), by = bitsOf(y);
					const auto n = static_cast<size_t>(std::stoi(bx.substr(9, 15), nullptr, 2));
					std::vector<std::string> rx, ry;
					for(size_t c = 0; c < n && 24 + 30 * (c + 1) <= bx.size(); ++c)
					{
						rx.push_back(bx.substr(24 + 30 * c, 30));
						ry.push_back(by.substr(24 + 30 * c, 30));
					}
					std::sort(rx.begin(), rx.end());
					std::sort(ry.begin(), ry.end());
					eq = rx == ry && bx.substr(0, 24) == by.substr(0, 24);
				}
				else if(eq && i == 0 && x.size() > 11)	// header + name: the header's last 3 bits (unknown4) are the OS's
				{
					auto mx = x, my = y;
					mx[10] &= 0xf8;
					my[10] &= 0xf8;
					eq = mx == my;
				}
				else if(eq && !x.empty())
					eq = std::equal(x.begin(), x.end() - 1, y.begin());
				if(!eq)
					semanticDiff.push_back(i);
			}
			std::printf("SEMANTIC: %s", semanticDiff.empty() ? "same patch" : "differs in sections");
			for(const auto i : semanticDiff)
				std::printf(" %zu", i);
			std::printf("\n");
			pid = pid2;	// the note below goes to the patch now in the slot
		}
	}

	// G1_RAMDUMP=file: the CPU's 1 MB of RAM once the patch and its knobs are in, to find the OS's
	// own tables by diffing two runs that differ in one thing.
	if(const char* rd = std::getenv("G1_RAMDUMP"))
	{
		std::ofstream out(rd, std::ios::binary);
		for(uint32_t a = 0x100000; a < 0x200000; ++a)
			out.put(static_cast<char>(mc.read8(a)));
	}

	for(uint32_t d = 0; d < mc.dspCount(); ++d)
		mc.getDsp(d).resetLinkPeak();
	// G1_PCWATCH=174,194: how many times each DSP goes through those addresses during the note
	// (only counted at the start of a JIT block: fine for loops and branches).
	std::vector<uint32_t> watch;
	if(const char* w = std::getenv("G1_PCWATCH"))
		for(const auto& t : juce::StringArray::fromTokens(w, ",", ""))
			watch.push_back(static_cast<uint32_t>(t.getHexValue32()));
	for(uint32_t d = 0; d < mc.dspCount(); ++d)
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
	// "h2.7" holds a button down and "u2.7" lets it go: Shift pressed again in the middle.
	auto gesture = [&](const juce::String& _step) -> bool
	{
		if(_step.startsWithChar('h') || _step.startsWithChar('u'))
		{
			button(_step.substring(1), _step.startsWithChar('h'));
			return true;
		}
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
		takeTx(mc, out);
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
	// G1_CHORD=60,64,67 presses those notes through the PC Port like the editor's keyboard, holds
	// them 0.5 s, releases them, waits 0.5 s, and only then measures: what is left is a stuck note.
	// G1_SEQ=+60,+64,-60,-64 plays that order instead (+ press, then 100 ms; - release, then
	// G1_CHORD_MS). G1_CHORD_MS is the gap after each message (default 5 ms); G1_CHORD_MIDI=1 sends
	// them through MIDI IN instead. The OS treats the editor's notes as one key (see NOTES.md, "The
	// editor's keyboard is one key"), so overlapping notes stick on the real G1 as well.
	if(const char* ch = std::getenv("G1_CHORD"))
	{
		capture = false;
		const auto gap = static_cast<uint64_t>(std::getenv("G1_CHORD_MS") ? std::atoi(std::getenv("G1_CHORD_MS")) : 5);
		const bool viaMidi = std::getenv("G1_CHORD_MIDI") && *std::getenv("G1_CHORD_MIDI") && *std::getenv("G1_CHORD_MIDI") != '0';
		auto noteMsg = [&](const int _n, const bool _on)
		{
			if(viaMidi)
				mc.getSci().write({static_cast<uint8_t>(_on ? 0x90 : 0x80), static_cast<uint8_t>(_n), static_cast<uint8_t>(_on ? 100 : 0)});
			else
				mc.getPcPort().receive(withChecksum({0xf0, 0x33, 0x5c, 0x06, static_cast<uint8_t>(pid), 0x56, static_cast<uint8_t>(_on ? 0 : 1), static_cast<uint8_t>(_n)}));
			run(mc, gap * g_ms);
		};
		auto parse = [](const char* _s, auto _each)
		{
			for(const char* c = _s; *c; )
			{
				_each(c);
				while(*c && *c != ',') ++c;
				if(*c == ',') ++c;
			}
		};
		if(const char* seq = std::getenv("G1_SEQ"))
			parse(seq, [&](const char* _c)
			{
				const bool on = *_c == '+';
				noteMsg(std::atoi(_c + 1), on);
				if(on) run(mc, 100 * g_ms);
			});
		else
		{
			std::vector<int> notes;
			parse(ch, [&](const char* _c) { notes.push_back(std::atoi(_c)); });
			for(const auto n : notes) noteMsg(n, true);
			run(mc, 500 * g_ms);
			for(const auto n : notes) noteMsg(n, false);
		}
		run(mc, 500 * g_ms);
		std::printf("notes pressed and released; measuring what is left\n");
		blocks.clear();
		capture = true;
	}
	else if(const char* mn = std::getenv("G1_MIDINOTE"))
	{
		const auto ch = static_cast<uint8_t>((std::atoi(mn) - 1) & 0x0f);
		mc.getSci().write({static_cast<uint8_t>(0x90 | ch), static_cast<uint8_t>(note), 100});
	}
	auto sendNote = [&]
	{
		const auto on = withChecksum({0xf0, 0x33, 0x5c, 0x06, static_cast<uint8_t>(pid), 0x56, 0x00, static_cast<uint8_t>(note)});
		mc.getPcPort().receive(on);
	};
	// --note -1: no note at all, to hear whether the patch sounds by itself. With --note-at the
	// note comes later, during the measurement (see below): one boot measures both halves.
	if(note >= 0 && noteAt >= 0.0 && events.empty())
	{
		events.emplace_back(noteAt, true);
		if(noteOffAt > noteAt)
			events.emplace_back(noteOffAt, false);
	}
	const bool noteLater = note >= 0 && !events.empty();
	if(!std::getenv("G1_CHORD") && !std::getenv("G1_MIDINOTE") && note >= 0 && !noteLater)
		sendNote();
	// G1_REALTIME=seconds: plays the note in real time with EmuHost's own pacing and AudioBridge,
	// against a simulated sound card (48 kHz, 256-frame periods), and prints once per second the
	// dropouts, how far the emulator is behind the wall clock and how full the output queue is.
	if(const char* rt = std::getenv("G1_REALTIME"))
	{
		using clock = std::chrono::steady_clock;
		g1app::AudioBridge bridge(1.0f);
		bridge.setRate(48000);
		mc.outputDsp().setBlockCallback([&](const int32_t _a, const int32_t _b, const int32_t _c, const int32_t _d)
		{
			bridge.push(_a, _b, _c, _d);
		});
		std::atomic<bool> quit{false};
		std::thread card([&]
		{
			constexpr size_t n = 256;
			std::array<std::vector<float>, 4> o;
			for(auto& v : o) v.resize(n);
			std::array<float*, 4> op{o[0].data(), o[1].data(), o[2].data(), o[3].data()};
			std::vector<float> in(n, 0.0f);
			std::array<const float*, 2> ip{in.data(), in.data()};
			auto next = clock::now();
			while(!quit)
			{
				next += std::chrono::nanoseconds(static_cast<int64_t>(1e9 * n / 48000.0));
				std::this_thread::sleep_until(next);
				bridge.render(op.data(), 4, ip.data(), 2, n);
			}
		});
		// G1_RT_NOTES=60,64,67: more notes through MIDI IN (channel 1) before it starts.
		// G1_RT_KNOB=k,ms,from,to: knob k (1-18) swept up and down every ms, between seconds from and to.
		if(const char* rn = std::getenv("G1_RT_NOTES"))
			for(const auto& t : juce::StringArray::fromTokens(rn, ",", ""))
				mc.getSci().write({0x90, static_cast<uint8_t>(t.getIntValue()), 100});
		int knob = 0, knobMs = 10;
		double knobFrom = 0, knobTo = 0;
		if(const char* rk = std::getenv("G1_RT_KNOB"))
		{
			const auto f = juce::StringArray::fromTokens(rk, ",", "");
			knob = f[0].getIntValue();
			knobMs = std::max(1, f[1].getIntValue());
			knobFrom = f[2].getDoubleValue();
			knobTo = f[3].getDoubleValue();
		}
		static constexpr std::array<uint8_t, 18> adc = {0x31, 0x37, 0x2d, 0x32, 0x28, 0x2e, 0x33, 0x29, 0x2f, 0x34, 0x2a, 0x1a, 0x35, 0x2b, 0x1b, 0x36, 0x2c, 0x1c};
		auto lastKnob = clock::now();
		int knobValue = 0, knobStep = 7;
		const auto start = clock::now();
		const auto base = mc.ucCycles();
		auto lastReport = start;
		uint64_t lastXruns = 0;
		const double total = std::atof(rt);
		for(;;)
		{
			const auto t0 = clock::now();
			const double elapsed = std::chrono::duration<double>(t0 - start).count();
			if(elapsed >= total)
				break;
			if(knob >= 1 && knob <= 18 && elapsed >= knobFrom && elapsed < knobTo && t0 - lastKnob >= std::chrono::milliseconds(knobMs))
			{
				lastKnob = t0;
				knobValue += knobStep;
				if(knobValue <= 0 || knobValue >= 255) { knobStep = -knobStep; knobValue = std::clamp(knobValue, 0, 255); }
				mc.setAdc(adc[static_cast<size_t>(knob - 1)], static_cast<uint8_t>(knobValue));
			}
			const auto target = base + static_cast<uint64_t>(elapsed * g1::g_ucClock);
			const auto limit = mc.ucCycles() + g1::g_ucClock / 500;
			while(mc.ucCycles() < target && mc.ucCycles() < limit)
				mc.exec();
			std::vector<uint8_t> drop;
			takeTx(mc, drop);
			if(mc.ucCycles() >= target)
			{
				mc.setIdle(true);
				std::this_thread::sleep_for(std::chrono::microseconds(500));
				mc.setIdle(false);
			}
			const auto t1 = clock::now();
			if(t1 - lastReport >= std::chrono::seconds(1))
			{
				lastReport = t1;
				const auto x = bridge.xruns();
				const double behindMs = target > mc.ucCycles() ? 1000.0 * static_cast<double>(target - mc.ucCycles()) / g1::g_ucClock : 0.0;
				std::printf("RT t=%5.1f s  dropouts %llu  behind %.1f ms\n", std::chrono::duration<double>(t1 - start).count(),
					static_cast<unsigned long long>(x - lastXruns), behindMs);
				std::fflush(stdout);
				lastXruns = x;
			}
		}
		quit = true;
		card.join();
		mc.syncDsps();
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
			std::printf("RT DSP%u overruns=%llu\n", d, static_cast<unsigned long long>(mc.getDsp(d).irqdOverruns()));
		return 0;
	}
	// G1_MIDICLOCK=bpm sends MIDI clock (a start, then $F8 at 24 per beat) into MIDI IN while it
	// plays: with the synth's clock set to external, that is what moves MIDIGlobal and whatever
	// follows it (sequencers, arpeggios).
	if(bench)
		mc.resetBenchStats();
	std::array<uint64_t, g1::g_maxDsps> benchCycles{}, benchInstructions{};
	if(bench)
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
		{
			benchCycles[d] = mc.getDsp(d).dsp().getCycles();
			benchInstructions[d] = mc.getDsp(d).dsp().getInstructionCounter();
		}
	if(cycleProfile >= 0)
		mc.getDsp(cycleProfile).startCycleProfile();
	const auto benchStart = std::chrono::steady_clock::now();
	if(const char* mcl = std::getenv("G1_MIDICLOCK"))
	{
		const double tickMs = 60000.0 / (std::atof(mcl) * 24.0);
		mc.getSci().write({0xfa});
		double t = 0;
		bool noteSent = false;
		while(t < seconds * 1000)
		{
			if(noteLater && !noteSent && t >= events.front().first * 1000)
			{
				sendNote();
				noteSent = true;
			}
			mc.getSci().write({0xf8});
			run(mc, static_cast<uint64_t>(tickMs * static_cast<double>(g_ms)));
			t += tickMs;
		}
	}
	else if(noteLater || !paramsAt.empty() || !addsAt.empty() || !knobsAt.empty())
	{
		// Each event at its time, in whole milliseconds like the rest of the bench: the note's
		// presses and releases (when it plays later) and the parameter changes.
		struct Ev { double at; int kind; size_t i; };	// kind 0 note event, 1 parameter
		std::vector<Ev> timeline;
		if(noteLater)
			for(size_t k = 0; k < events.size(); ++k)
				timeline.push_back({events[k].first, 0, k});
		for(size_t k = 0; k < paramsAt.size(); ++k)
			timeline.push_back({paramsAt[k].at, 1, k});
		for(size_t k = 0; k < addsAt.size(); ++k)
			timeline.push_back({addsAt[k].at, 2, k});
		for(size_t k = 0; k < knobsAt.size(); ++k)
			timeline.push_back({knobsAt[k].at, 3, k});
		std::stable_sort(timeline.begin(), timeline.end(), [](const Ev& _a, const Ev& _b) { return _a.at < _b.at; });
		uint64_t nowMs = 0;
		for(const auto& ev : timeline)
		{
			const auto atMs = static_cast<uint64_t>(std::min(ev.at, seconds) * 1000);
			if(atMs > nowMs)
				run(mc, (atMs - nowMs) * g_ms);
			nowMs = std::max(nowMs, atMs);
			if(ev.kind == 0)
				mc.getPcPort().receive(withChecksum({0xf0, 0x33, 0x5c, 0x06, static_cast<uint8_t>(pid), 0x56, static_cast<uint8_t>(events[ev.i].second ? 0 : 1), static_cast<uint8_t>(note)}));
			else if(ev.kind == 3)
			{
				const auto adc = g1::KnobMap::KnobAdc[static_cast<size_t>(knobsAt[ev.i].knob - 1)];
				for(int v = 0; v <= 255; v += 4)
				{
					mc.setAdc(adc, static_cast<uint8_t>(v));
					run(mc, 6 * g_ms);
				}
				nowMs += 6 * 64;
				std::printf("knob %d swept at %.2f s\n", knobsAt[ev.i].knob, knobsAt[ev.i].at);
			}
			else if(ev.kind == 2)
			{
				const auto& aa = addsAt[ev.i];
				std::vector<int> defaults;
				std::string name = "Added";
				if(const auto* d = descs.getModuleByIndex(aa.type))
				{
					name = d->name.toStdString();
					for(const auto& p : d->parameters)
						if(p.paramClass == "parameter")
							defaults.push_back(p.defaultValue);
				}
				const NewModuleMessageProto msg(pid, aa.type, 1, aa.index, aa.x, aa.y, name, defaults);
				mc.getPcPort().receive(msg.toSysEx(0));
				std::printf("added module %d (type %d, %zu parameters) at %.2f s\n", aa.index, aa.type, defaults.size(), aa.at);
			}
			else
			{
				const auto& pa = paramsAt[ev.i];
				mc.getPcPort().receive(withChecksum({0xf0, 0x33, 0x4c, 0x06, static_cast<uint8_t>(pid), 0x40, static_cast<uint8_t>(pa.section), static_cast<uint8_t>(pa.module), static_cast<uint8_t>(pa.param), static_cast<uint8_t>(pa.value)}));
			}
		}
		const auto endMs = static_cast<uint64_t>(seconds * 1000);
		if(endMs > nowMs)
			run(mc, (endMs - nowMs) * g_ms);
	}
	else
		run(mc, static_cast<uint64_t>(seconds * 1000) * g_ms);
	capture = false;
	// What the G1 sent out of its MIDI OUT meanwhile (a knob's CC, a Program Change...).
	{
		std::vector<uint8_t> midiOut;
		mc.getSci().read(midiOut);
		std::printf("MIDI OUT: %zu bytes", midiOut.size());
		for(size_t k = 0; k < midiOut.size() && k < 48; ++k)
			std::printf(" %02x", midiOut[k]);
		std::printf("%s\n", midiOut.size() > 48 ? " ..." : "");
	}
	if(bench)
	{
		const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - benchStart).count();
		const auto s = mc.benchStats();
		const double scale = wall > 0 ? 1.0 / wall : 0.0;
		std::printf("BENCH emulated_s=%.3f wall_s=%.6f realtime=%.3f periodic_barriers_s=%.1f host_port_barriers_s=%.1f\n",
			seconds, wall, wall > 0 ? seconds / wall : 0.0, s.periodicBarriers * scale, s.hostPortBarriers * scale);
		std::printf("BENCH thread=cpu busy_s=%.6f waiting_s=%.6f\n", s.cpuBusyNs / 1e9, s.cpuWaitingNs / 1e9);
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
			std::printf("BENCH thread=dsp%u busy_s=%.6f waiting_s=%.6f\n", d, s.threadBusyNs[d] / 1e9, s.threadWaitingNs[d] / 1e9);
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
		{
			std::printf("BENCH dsp=%u blocks_s=%.1f overruns=%llu\n", d, s.blocks[d] * scale,
				static_cast<unsigned long long>(mc.getDsp(d).irqdOverruns()));
			const auto cycles = mc.getDsp(d).dsp().getCycles() - benchCycles[d];
			const auto instructions = mc.getDsp(d).dsp().getInstructionCounter() - benchInstructions[d];
			std::printf("BENCH_CYCLES dsp=%u cycles=%llu core_instructions=%llu cycles_per_instruction=%.6f\n", d,
				static_cast<unsigned long long>(cycles), static_cast<unsigned long long>(instructions),
				instructions ? double(cycles) / instructions : 0.0);
		}
	}
	if(cycleProfile >= 0 && !mc.getDsp(cycleProfile).printCycleProfile(stdout))
	{
		std::fprintf(stderr, "cycle profile failed to account for every emulated cycle\n");
		return 1;
	}
	std::vector<uint8_t> rest;
	takeTx(mc, rest);
	// $7F is the plain ACK the OS sends for each packet (NME treats it as one), so it is not
	// worth reporting; what matters is whether the patch ended up loaded and sounding.
	if(std::getenv("G1_VERBOSE") && !rest.empty())
		std::printf("the OS said after the note: %s\n", hex(rest, 40).c_str());

	if(std::getenv("G1_VERBOSE"))
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
		{
			auto& p = mc.getDsp(d).periph();
			for(uint32_t ch = 2; ch < 4; ++ch)
				std::printf("DSP%u  DMA%u DCR=%06x DSR=%06x DDR=%06x DCO=%06x\n", d, ch, p.getDMA().getDCR(ch), p.getDMA().getDSR(ch), p.getDMA().getDDR(ch), p.getDMA().getDCO(ch));
			std::printf("DSP%u  ESSI0 SR=%06x CRB=%06x RX=%06x | ESSI1 SR=%06x CRB=%06x RX=%06x\n", d,
				static_cast<uint32_t>(p.getEssi0().getSR()), static_cast<uint32_t>(p.getEssi0().getCRB()), 0u,
				static_cast<uint32_t>(p.getEssi1().getSR()), static_cast<uint32_t>(p.getEssi1().getCRB()), 0u);
			{
				auto& dd = mc.getDsp(d);
				std::printf("DSP%u  booted=%d boots=%u pc=$%04x cyc/ins=%.3f irqd=%llu of %llu blocks, overruns=%llu, stalls=%llu, host commands=%llu, words to host=%llu\n", d,
					static_cast<int>(dd.booted()), dd.bootCount(), static_cast<unsigned>(dd.dsp().getPC().toWord()), static_cast<double>(dd.dsp().getCycles()) / static_cast<double>(dd.dsp().getInstructionCounter()),
					static_cast<unsigned long long>(dd.irqdCount()), static_cast<unsigned long long>(dd.dsp().getCycles() / 864), static_cast<unsigned long long>(dd.irqdOverruns()),
					static_cast<unsigned long long>(dd.stalls()), static_cast<unsigned long long>(dd.hostCommands()), static_cast<unsigned long long>(dd.wordsToHost()));
			}
			{ std::printf("DSP%u  vectors:", d); for(auto& [v, n] : mc.getDsp(d).servicedVectors()) std::printf(" $%02x=%llu", v, static_cast<unsigned long long>(n)); std::printf("\n"); }
			std::printf("DSP%u  DOR0=%06x DOR1=%06x DCO1=%06x (as read by the program: X:$FFFFF3/F2/E9 = %06x %06x %06x)\n", d,
				p.getDMA().getDOR(0), p.getDMA().getDOR(1), p.getDMA().getDCO(1),
				p.read(0xfffff3, dsp56k::Instruction::Invalid), p.read(0xfffff2, dsp56k::Instruction::Invalid), p.read(0xffffe9, dsp56k::Instruction::Invalid));
		}
	for(const auto a : watch)
	{
		std::printf("PC $%04x:", a);
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
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
			takeTx(mc, drain);
			mc.setAdc(code, 200);
			run(mc, 250 * g_ms);
			std::vector<uint8_t> outMsgs;
			takeTx(mc, outMsgs);
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
	for(uint32_t d = 0; d + 1 < mc.dspCount(); ++d)
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

	// G1_RAMDUMP=file: the CPU's RAM ($100000-$1FFFFF) at the end, raw, like g1boot's.
	if(const char* rd = std::getenv("G1_RAMDUMP"))
		if(FILE* f = std::fopen(rd, "wb"))
		{
			for(uint32_t a = 0x100000; a < 0x200000; ++a)
			{
				const uint8_t b = mc.read8(a);
				std::fwrite(&b, 1, 1, f);
			}
			std::fclose(f);
		}

	// G1_DUMP=dir: P, X and Y memory of each DSP at the end (to disassemble with dspdis).
	if(const char* dir = std::getenv("G1_DUMP"))
		for(uint32_t d = 0; d < mc.dspCount(); ++d)
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
