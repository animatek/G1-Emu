// g1boot: boots the G1 OS on the emulated 68331 and reports what happens.
//
//   g1boot ROM [millions_of_instructions]
//   g1boot ROM dis FROM TO            (disassembly, hex addresses)
//
// Prints where the PC goes, which loops it gets stuck in, the chip-selects the
// boot programs (= the real memory map) and the accesses to hardware that is
// not emulated yet. The ROM is the user's and never goes into the repo.

#include "g1Lib/g1mc.h"

#include <algorithm>
#include <cstdlib>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
	// 68331 SIM chip-selects: CSBARBT/CSORBT and CSBAR0..10/CSOR0..10.
	void printChipSelects(g1::Microcontroller& _mc)
	{
		static const char* sizes[] = {"2K", "8K", "16K", "64K", "128K", "256K", "512K", "1M"};
		const uint16_t cspar0 = _mc.read16(0xfffa44), cspar1 = _mc.read16(0xfffa46);
		std::printf("\nchip-selects (CSPAR0=%04x CSPAR1=%04x):\n", cspar0, cspar1);
		for(int i = -1; i <= 10; ++i)
		{
			const uint32_t barAddr = i < 0 ? 0xfffa48 : 0xfffa4c + static_cast<uint32_t>(i) * 4;
			const uint16_t bar = _mc.read16(barAddr), orr = _mc.read16(barAddr + 2);
			if(!orr && !bar)
				continue;
			const uint32_t base = static_cast<uint32_t>(bar & 0xfff8) << 8;
			const int mode = (orr >> 13) & 3;	// 0 off, 1 lower, 2 upper, 3 both bytes
			const char* rw[] = {"-", "R", "W", "R/W"};
			std::printf("  CS%-4s base=$%06x size=%-4s bytes=%d %s dsack=%d\n", i < 0 ? "BOOT" : std::to_string(i).c_str(),
				base, sizes[bar & 7], mode, rw[(orr >> 11) & 3], (orr >> 6) & 0xf);
		}
	}
}

int main(int argc, char** argv)
{
	if(argc < 2)
	{
		std::fprintf(stderr, "usage: g1boot ROM [millions_of_instructions]\n");
		return 2;
	}
	std::ifstream f(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if(rom.size() != g1::g_romSize)
	{
		std::fprintf(stderr, "the ROM must be 512 KB (it is %zu)\n", rom.size());
		return 1;
	}
	g1::Microcontroller mc(rom);

	// g1boot ROM dis FROM TO: disassembles (hex addresses)
	if(argc > 4 && std::string(argv[2]) == "dis")
	{
		for(uint32_t pc = std::stoul(argv[3], nullptr, 16), end = std::stoul(argv[4], nullptr, 16); pc < end;)
		{
			char dis[128];
			const auto len = mc.disassemble(pc, dis);
			std::printf("$%06x  %s\n", pc, dis);
			pc += len ? len : 2;
		}
		return 0;
	}

	const uint64_t steps = (argc > 2 ? std::stoull(argv[2]) : 20) * 1000000ull;
	mc.installRomOsInFlash();
	for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		for(auto pcw : {0xcau, 0xccu, 0xd2u, 0x175u, 0x200u, 0x9eu, 0xc4u})
			mc.getDsp(d).pcWatch()[pcw] = 0;

	// History of what leaves each DSP's ESSI0 (distinct values, min, max)
	struct Tap { int32_t mn = 0x7fffffff, mx = -0x7fffffff; std::map<int32_t, uint64_t> values; uint64_t n = 0; };
	std::array<std::array<Tap, 2>, g1::g_dspCount> taps;
	// G1_TAP=file: slot 0 samples of each DSP's ESSI0, raw (int32 per DSP and frame).
	FILE* tapFile = std::getenv("G1_TAP") ? std::fopen(std::getenv("G1_TAP"), "wb") : nullptr;
	std::array<int32_t, g1::g_dspCount> tapFrame{};
	for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		mc.getDsp(d).setAudioCallback([&taps, &tapFrame, tapFile, d](const uint32_t _essi, const int32_t _l, const int32_t)
		{
			if(tapFile && _essi == 0)
			{
				tapFrame[d] = _l;
				if(d == g1::g_dspCount - 1) std::fwrite(tapFrame.data(), sizeof(int32_t), tapFrame.size(), tapFile);
			}
			auto& t = taps[d][_essi];
			t.mn = std::min(t.mn, _l); t.mx = std::max(t.mx, _l); ++t.n;
			if(t.values.size() < 64) ++t.values[_l];
		});
	// G1_BLOCKS=file: DSP 3's output block by block (one sample at 96 kHz): 4 int32 channels
	// (outputs 1-4), raw. It is what g1run sends to the sound card.
	FILE* blockFile = std::getenv("G1_BLOCKS") ? std::fopen(std::getenv("G1_BLOCKS"), "wb") : nullptr;
	if(blockFile)
		mc.getDsp(g1::g_dspCount - 1).setBlockCallback([blockFile](const int32_t _a, const int32_t _b, const int32_t _c, const int32_t _d)
		{
			const int32_t f[4] = {_a, _b, _c, _d};
			std::fwrite(f, sizeof(int32_t), 4, blockFile);
		});
	// G1_ADCMUX=code: that ADC channel (a knob) at $FF from power-on, to identify it
	// G1_ADCALL=value (hex): all ADC channels at that value from power-on.
	if(const char* all = std::getenv("G1_ADCALL"))
		for(const uint8_t code : {0x31, 0x37, 0x2d, 0x32, 0x28, 0x2e, 0x33, 0x29, 0x2f, 0x34, 0x2a, 0x1a, 0x35, 0x2b, 0x1b, 0x36, 0x2c, 0x1c, 0x30, 0x18})
			mc.setAdc(code, static_cast<uint8_t>(std::stoul(all, nullptr, 16)));
	if(const char* mux = std::getenv("G1_ADCMUX"))
		mc.setAdc(static_cast<uint8_t>(std::stoul(mux, nullptr, 16)), 0xff);
	std::printf("reset: PC=$%06x SP=$%06x\n", mc.getPC(), mc.getAReg(7));
	std::fflush(stdout);

	// G1_WATCH="123e94,1239e8": counts how many times the PC goes through those addresses and
	// shows the registers the first times.
	std::map<uint32_t, uint32_t> watch;
	if(const char* w = std::getenv("G1_WATCH"))
	{
		std::string list = w;
		size_t pos = 0;
		while(pos < list.size())
		{
			const auto comma = list.find(',', pos);
			watch[static_cast<uint32_t>(std::stoul(list.substr(pos, comma - pos), nullptr, 16))] = 0;
			if(comma == std::string::npos) break;
			pos = comma + 1;
		}
	}
	std::array<uint64_t, 8> iplHist{};
	std::array<uint32_t, 64> lastPcs{};	// last PCs, to see how a crash is reached
	size_t lastPcPos = 0;
	std::map<uint32_t, uint64_t> pcHits;	// PC -> count, to see which loop it is stuck in
	uint64_t cycles = 0;
	uint32_t lastReport = 0;
	// Halfway through, NME's handshake (IAm, version 3.3) is sent over the UART
	// and everything the OS sends out on it is logged.
	std::vector<uint8_t> sciOut;
	bool iamSent = false;
	// g1boot ROM N replay FILE: everything NME sent in a session (recorded by g1run in
	// pcport-in.bin) is fed into the PC Port along the run.
	std::vector<uint8_t> replay;
	// g1boot ROM N diff A.bin B.bin: A at 1/4, B at 1/2; the PCs executed in [3/8, 1/2)
	// (idle after A) and in [1/2, 5/8) (after B) are compared.
	std::vector<uint8_t> diffB;
	std::set<uint32_t> pcIdle, pcAfter;
	const bool diffMode = argc > 5 && std::string(argv[3]) == "diff";
	if(diffMode)
	{
		std::ifstream fa(argv[4], std::ios::binary);
		replay.assign(std::istreambuf_iterator<char>(fa), std::istreambuf_iterator<char>());
		std::ifstream fb(argv[5], std::ios::binary);
		diffB.assign(std::istreambuf_iterator<char>(fb), std::istreambuf_iterator<char>());
		iamSent = true;
	}
	std::vector<std::vector<uint8_t>> replayMsgs;
	size_t replayNext = 0;
	std::vector<uint8_t> pcOutSoFar;	// what the OS has sent on the PC Port so far
	size_t pcOutScan = 0;
	std::array<int, 4> slotPid{-1, -1, -1, -1};
	uint32_t pidRewrites = 0;
	if(argc > 4 && std::string(argv[3]) == "replay")
	{
		std::ifstream rf(argv[4], std::ios::binary);
		replay.assign(std::istreambuf_iterator<char>(rf), std::istreambuf_iterator<char>());
		iamSent = true;	// the replay carries its own IAm
		// Split into messages to send them spaced out, like NME (which waits for each ACK).
		std::vector<uint8_t> cur;
		for(auto b : replay)
		{
			if(b == 0xf0) cur.clear();
			cur.push_back(b);
			if(b == 0xf7) { replayMsgs.push_back(cur); cur.clear(); }
		}
	}
	for(uint64_t i = 0; i < steps; ++i)
	{
		if(!iamSent && i >= steps / 2)
		{
			iamSent = true;
			mc.getPcPort().receive({0xf0, 0x33, 0x00, 0x06, 0x00, 0x03, 0x03, 0xf7});
			std::printf("  >> IAm sent on the PC PORT at instruction %llu\n", static_cast<unsigned long long>(i));
		}
		if(diffMode)
		{
			if(i >= steps * 3 / 8 && i < steps / 2) pcIdle.insert(mc.getPC());
			else if(i >= steps / 2 && i < steps * 5 / 8) pcAfter.insert(mc.getPC());
			if(i == steps / 2) { mc.getPcPort().receive(diffB); std::printf("  >> diff: %zu bytes (B)\n", diffB.size()); }
		}
		if(std::getenv("G1_MARK") && i >= steps * 9 / 10 && (i % 20000) == 0)
		{
			// Mark DSP 0's audio buffers to see which one leaves through the ESSI
			auto& mem = mc.getDsp(0).dsp().memory();
			for(dsp56k::TWord a = 0x6c0; a < 0x700; ++a) { mem.set(dsp56k::MemArea_Y, a, 0x111111 + (a & 0x3f)); mem.set(dsp56k::MemArea_X, a, 0x222222 + (a & 0x3f)); }
			if(i == steps * 9 / 10) for(auto& t : taps[0]) t = {};
		}
		if(!replay.empty() && !diffMode && i == steps * 3 / 4)
		{
			mc.getSci().write({0x90, 60, 100});	// held note on MIDI IN, channel 1
			std::printf("  >> note 60 on via MIDI IN\n");
			for(uint32_t d = 0; d < g1::g_dspCount; ++d)
				for(auto& t : taps[d]) t = {};
		}
		if(!replayMsgs.empty() && replayNext < replayMsgs.size() && i >= steps / 10 && (i % 500000) == 0)
		{
			// Each slot's PID is decided by the OS when it receives a patch (ACK $36). The recorded
			// session may carry others (NME reconnected to a rebooted G1): they are rewritten
			// with the ones this OS gave and the checksum is redone.
			mc.getPcPort().takeTx(pcOutSoFar);
			for(; pcOutScan + 7 < pcOutSoFar.size(); ++pcOutScan)
			{
				const auto* m = &pcOutSoFar[pcOutScan];
				if(m[0] == 0xf0 && m[1] == 0x33 && (m[2] >> 2) == 0x16 && m[3] == 0x06 && m[5] == 0x36)
					slotPid[m[2] & 3] = m[6];
			}
			auto msg = replayMsgs[replayNext++];
			if(msg.size() > 6 && msg[0] == 0xf0 && msg[1] == 0x33 && msg[3] == 0x06)
			{
				const auto cc = msg[2] >> 2;
				const auto slot = msg[2] & 3;
				uint8_t* pid = nullptr;
				if((cc == 0x13 || cc == 0x17) && msg[4] != 0x41) pid = &msg[4];			// Parameter, PatchModification
				else if(cc >= 0x1c && cc <= 0x1f && !(msg[4] & 0x40)) pid = &msg[4];	// PatchPacket of an already loaded patch
				if(pid && slotPid[slot] >= 0 && (*pid & 0x3f) != slotPid[slot])
				{
					*pid = static_cast<uint8_t>((*pid & 0x40) | slotPid[slot]);
					uint32_t sum = 0;
					for(size_t k = 0; k + 2 < msg.size(); ++k) sum += msg[k];
					msg[msg.size() - 2] = static_cast<uint8_t>(sum & 0x7f);
					++pidRewrites;
				}
			}
			mc.getPcPort().receive(msg);
			if(replayNext == replayMsgs.size())
				std::printf("  >> replay: %zu messages sent (the last one at instruction %llu)\n", replayMsgs.size(), static_cast<unsigned long long>(i));
		}
		if((i & 0xfff) == 0)
			mc.getSci().read(sciOut);
		const auto pc = mc.getPC();
		if(i > steps / 2)
			++pcHits[pc];
		if(pc >= g1::g_memSize || (mc.readImm16(pc) == 0 && mc.readImm16(pc + 2) == 0))
		{
			if(pc < g1::g_memSize)
				std::printf("PC in empty memory (opcode 0000): $%06x\n", pc);
			std::printf("PC outside ROM/RAM: $%06x after %llu instructions. Last PCs:\n", pc, static_cast<unsigned long long>(i));
			for(size_t k = 0; k < lastPcs.size(); ++k)
			{
				const auto p = lastPcs[(lastPcPos + k) % lastPcs.size()];
				char dis[128];
				mc.disassemble(p, dis);
				std::printf("    $%06x  %s\n", p, dis);
			}
			std::printf("  A7=$%06x D0=%08x D1=%08x A0=$%06x A1=$%06x\n", mc.getAReg(7), mc.getDReg(0), mc.getDReg(1), mc.getAReg(0), mc.getAReg(1));
			break;
		}
		lastPcs[lastPcPos++ % lastPcs.size()] = pc;
		if((i & 0xff) == 0)
			++iplHist[(mc.getSR() >> 8) & 7];
		if(!watch.empty())
		{
			auto it = watch.find(pc);
			if(it != watch.end() && it->second++ < 6)
			{
				std::printf("  [watch $%06x] #%u D0=%08x D1=%08x D2=%08x D3=%08x A0=%06x A5=%06x A7=%06x ret=$%06x\n       (A5):", pc, it->second,
					mc.getDReg(0), mc.getDReg(1), mc.getDReg(2), mc.getDReg(3), mc.getAReg(0), mc.getAReg(5), mc.getAReg(7),
					(static_cast<uint32_t>(mc.read16(mc.getAReg(7))) << 16) | mc.read16(mc.getAReg(7) + 2));
				for(uint32_t k = 0; k < 28; ++k) std::printf(" %02x", mc.read8(mc.getAReg(5) + k));
				std::printf("\n       A6-$48..-$24:");
				for(uint32_t k = 0; k < 0x24; k += 4)
					std::printf(" %08x", (static_cast<uint32_t>(mc.read16(mc.getAReg(6) - 0x48 + k)) << 16) | mc.read16(mc.getAReg(6) - 0x48 + k + 2));
				std::printf("\n");
			}
		}
		cycles += mc.exec();
		const auto report = static_cast<uint32_t>(i / (steps / 10));
		if(report != lastReport)
		{
			lastReport = report;
			std::printf("  %3u%%  PC=$%06x  cycles=%llu  DSP:", report * 10, mc.getPC(), static_cast<unsigned long long>(cycles));
			for(uint32_t d = 0; d < g1::g_dspCount; ++d)
				std::printf(" %u:%s/%06x", d, mc.getDsp(d).booted() ? "on" : "boot", mc.getDsp(d).dsp().getPC().toWord());
			std::printf("\n");
			std::fflush(stdout);
		}
	}

	mc.getSci().read(sciOut);
	// G1_TRACE=N: DSP 0 block by block (N blocks of 864 cycles): IRQD, output buffers, DMA4 and what leaves on TX0.
	if(const char* tr = std::getenv("G1_TRACE"))
	{
		auto& d = mc.getDsp(0);
		std::vector<std::pair<int32_t, int32_t>> tx;
		d.setAudioCallback([&](const uint32_t _essi, const int32_t _l, const int32_t _r) { if(_essi == 0) tx.push_back({_l, _r}); });
		auto& mem = d.dsp().memory();
		auto& dma = d.periph().getDMA();
		uint64_t c = d.dsp().getCycles();
		for(int f = 0; f < std::atoi(tr); ++f)
		{
			const auto irqd0 = d.irqdCount();
			c += std::getenv("G1_TRACE_STEP") ? std::atoi(std::getenv("G1_TRACE_STEP")) : 864;
			d.catchUp(c);
			d.flushAudio();
			std::printf("t%03d irqd+%llu x4=%03x x5=%03x | Y6C0:", f, static_cast<unsigned long long>(d.irqdCount() - irqd0), mem.get(dsp56k::MemArea_X, 4), mem.get(dsp56k::MemArea_X, 5));
			for(dsp56k::TWord a = 0x6c0; a < 0x6c9; ++a) std::printf(" %06x", mem.get(dsp56k::MemArea_Y, a));
			std::printf(" | Y6E0:");
			for(dsp56k::TWord a = 0x6e0; a < 0x6e9; ++a) std::printf(" %06x", mem.get(dsp56k::MemArea_Y, a));
			std::printf(" | DMA4 DSR=%03x DCO=%x DE=%d | TX:", dma.getDSR(4), dma.getDCO(4), (dma.getDCR(4) >> 23) & 1);
			for(auto& [l, r] : tx) std::printf(" (%06x %06x)", l & 0xffffff, r & 0xffffff);
			tx.clear();
			std::printf("\n");
		}
	}
	// Is each DSP computing something? Internal X/Y memory before and after 200,000 instructions.
	{
		std::array<std::vector<uint32_t>, g1::g_dspCount> before;
		for(uint32_t d = 0; d < g1::g_dspCount; ++d)
			for(dsp56k::TWord a = 0; a < 0x800; ++a)
			{
				before[d].push_back(mc.getDsp(d).dsp().memory().get(dsp56k::MemArea_X, a));
				before[d].push_back(mc.getDsp(d).dsp().memory().get(dsp56k::MemArea_Y, a));
			}
		for(int k = 0; k < 200000; ++k) mc.exec();
		for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		{
			std::printf("DSP%u changes in internal X/Y:", d);
			uint32_t n = 0;
			for(dsp56k::TWord a = 0; a < 0x800; ++a)
				for(int xy = 0; xy < 2; ++xy)
				{
					const auto v = mc.getDsp(d).dsp().memory().get(xy ? dsp56k::MemArea_Y : dsp56k::MemArea_X, a);
					if(v != before[d][a * 2 + xy] && n++ < 14)
						std::printf(" %c:%03x=%06x", xy ? 'Y' : 'X', a, v);
				}
			std::printf("  (%u)\n", n);
		}
	}
	if(diffMode)
	{
		std::vector<uint32_t> only;
		for(auto p : pcAfter) if(!pcIdle.count(p)) only.push_back(p);
		std::printf("\nnew PCs after B: %zu. Ranges:\n", only.size());
		for(size_t k = 0; k < only.size();)
		{
			size_t j = k;
			while(j + 1 < only.size() && only[j + 1] - only[j] <= 16) ++j;
			std::printf("  $%06x-$%06x (%zu)\n", only[k], only[j], j - k + 1);
			k = j + 1;
		}
	}
	std::printf("\nQSM: QMCR=%04x QILR/QIVR=%04x SCCR0=%04x SCCR1=%04x SCSR=%04x  PORTQS/PQSPAR=%04x DDRQS=%04x\n",
		mc.read16(0xfffc00), mc.read16(0xfffc04), mc.read16(0xfffc08), mc.read16(0xfffc0a), mc.read16(0xfffc0c),
		mc.read16(0xfffc14), mc.read16(0xfffc16));
	std::printf("SIM: SIMCR=%04x  PORTE=%02x DDRE=%02x PEPAR=%02x  PORTF=%02x DDRF=%02x PFPAR=%02x  GPT: TMSK=%04x TFLG=%04x TCTL=%04x\n",
		mc.read16(0xfffa00), mc.read8(0xfffa11), mc.read8(0xfffa15), mc.read8(0xfffa17),
		mc.read8(0xfffa19), mc.read8(0xfffa1d), mc.read8(0xfffa1f),
		mc.read16(0xfff920), mc.read16(0xfff922), mc.read16(0xfff91e));
	std::printf("SCDR: %u reads, %u writes by the CPU\n", mc.sciDataReads(), mc.sciDataWrites());
	std::vector<uint8_t> pcOut = pcOutSoFar;
	mc.getPcPort().takeTx(pcOut);
	if(pidRewrites)
		std::printf("replay: %u messages with a rewritten pid (pid per slot: %d %d %d %d)\n", pidRewrites, slotPid[0], slotPid[1], slotPid[2], slotPid[3]);
	auto& pc = mc.getPcPort();
	std::printf("PC PORT: %u interrupts, %u bytes read by the OS, %u sent; writes MR=%u CSR=%u CR=%u THR=%u ACR=%u IMR=%u\n",
		mc.pcPortIrqs(), pc.rxCount(), pc.txCount(), pc.writes(0), pc.writes(1), pc.writes(2), pc.writes(3), pc.writes(4), pc.writes(5));
	std::printf("PC PORT -> out (%zu bytes):", pcOut.size());
	for(size_t k = 0; k < std::min<size_t>(pcOut.size(), 300); ++k)
		std::printf(" %02x", pcOut[k]);
	if(FILE* f = std::fopen("/tmp/g1_pcout.bin", "wb")) { std::fwrite(pcOut.data(), 1, pcOut.size(), f); std::fclose(f); }
	std::printf("\n");
	std::printf("SCI -> out (%zu bytes):", sciOut.size());
	for(size_t k = 0; k < std::min<size_t>(sciOut.size(), 300); ++k)
		std::printf(" %02x", sciOut[k]);
	std::printf("\n");

	std::vector<std::pair<uint64_t, uint32_t>> hot;
	for(auto& [pc, n] : pcHits) hot.push_back({n, pc});
	std::sort(hot.rbegin(), hot.rend());
	std::printf("\nsecond half, most repeated PCs (the loop it is stuck in):\n");
	for(size_t i = 0; i < std::min<size_t>(12, hot.size()); ++i)
	{
		char dis[128];
		mc.disassemble(hot[i].second, dis);
		std::printf("  $%06x  %10llu  %s\n", hot[i].second, static_cast<unsigned long long>(hot[i].first), dis);
	}

	// HI08 trace: for each port, the first writes (24-bit words rebuilt)
	{
		std::map<uint32_t, std::vector<std::string>> perPort;
		std::map<uint32_t, uint32_t> pendingHigh;
		const bool tailMode = true;
		for(const auto& a : mc.hostTrace())
		{
			const auto port = a.addr & ~7u;
			auto& v = perPort[port];
			if(v.size() >= 40 && !(port == 0x200018 && v.size() < 140) && !(port == 0x200000 && a.pc >= 0x100000 && tailMode)) continue;
			char buf[64];
			const auto reg = a.addr & 7;
			if(!a.write) { std::snprintf(buf, sizeof(buf), "r%u", reg); if(v.empty() || v.back() != buf) v.push_back(buf); continue; }
			if(reg == 4) { pendingHigh[port] = a.value; continue; }
			if(reg == 6) std::snprintf(buf, sizeof(buf), "TX:%06x", ((pendingHigh[port] & 0xff) << 16) | (a.value & 0xffff));
			else std::snprintf(buf, sizeof(buf), "w%u=%02x", reg, a.value & 0xff);
			v.push_back(buf);
		}
		std::printf("\nHI08 trace (first events per port):\n");
		for(auto& [port, v] : perPort)
		{
			std::printf("  $%06x (%zu events):", port, v.size());
			// for port $200000 the last 80 are shown (what arrives after creating the patch)
			const size_t from = (port == 0x200000 && v.size() > 80) ? v.size() - 80 : 0;
			for(size_t k = from; k < v.size() && k < from + 80; ++k) std::printf(" %s", v[k].c_str());
			std::printf("\n");
		}
	}

	// G1_FINDTX=word (hex): who sends that 24-bit word to a DSP (port, CPU PC).
	if(const char* ft = std::getenv("G1_FINDTX"))
	{
		const auto want = static_cast<uint32_t>(std::stoul(ft, nullptr, 16));
		std::map<uint32_t, uint32_t> high;
		uint32_t found = 0;
		for(size_t k = 0; k < mc.hostTrace().size(); ++k)
		{
			const auto& a = mc.hostTrace()[k];
			if(!a.write) continue;
			const auto port = a.addr & ~7u, reg = a.addr & 7;
			if(reg == 4) { high[port] = a.value; continue; }
			if(reg != 6) continue;
			const auto w = ((high[port] & 0xff) << 16) | (a.value & 0xffff);
			if(w == want && found++ < 10)
			{
				std::printf("G1_FINDTX %06x -> $%06x from PC=$%06x (event %zu). Before:", w, port, a.pc, k);
				for(size_t j = k >= 12 ? k - 12 : 0; j < k; ++j)
					if(mc.hostTrace()[j].write && (mc.hostTrace()[j].addr & ~7u) == port)
						std::printf(" [r%u=%x pc=%06x]", mc.hostTrace()[j].addr & 7, mc.hostTrace()[j].value, mc.hostTrace()[j].pc);
				std::printf("\n");
			}
		}
		std::printf("G1_FINDTX: %u times\n", found);
	}
	// Pointer tables to the HI08 ports used by the OS (RAM)
	std::printf("\npointers in RAM:");
	for(uint32_t a : {0x15bd60u, 0x15bd70u, 0x15bd80u, 0x15bd90u, 0x144640u})
	{
		std::printf("\n  $%06x:", a);
		for(uint32_t i = 0; i < 16; i += 4)
			std::printf(" %08x", (static_cast<uint32_t>(mc.read16(a + i)) << 16) | mc.read16(a + i + 2));
	}
	std::printf("\n  DSP count ($1ab91c) = %u\n", mc.read8(0x1ab91c));

	// Dump of each DSP's program memory (to disassemble with dspdis)
	for(uint32_t i = 0; i < g1::g_dspCount; ++i)
	{
		const auto path = "/tmp/g1_dsp" + std::to_string(i) + "_p.hex";
		if(FILE* f = std::fopen(path.c_str(), "w"))
		{
			auto& mem = mc.getDsp(i).dsp().memory();
			for(dsp56k::TWord a = 0; a < 0x1000; ++a)
				std::fprintf(f, "%06x\n", mem.get(dsp56k::MemArea_P, a));
			std::fclose(f);
		}
	}

	std::printf("\nESSI output (slot 0, TX0) per DSP:\n");
	for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		for(uint32_t e = 0; e < 2; ++e)
		{
			auto& t = taps[d][e];
			std::printf("  DSP%u E%u: %llu frames, min %d max %d, %zu distinct values:", d, e, static_cast<unsigned long long>(t.n), t.mn, t.mx, t.values.size());
			int k = 0;
			for(auto& [v, c] : t.values) { if(k++ >= 8) break; std::printf(" %06x(x%llu)", v & 0xffffff, static_cast<unsigned long long>(c)); }
			std::printf("\n");
		}

	std::printf("\nDSP (HI08 words/HC/replies, audio peaks):\n");
	for(uint32_t i = 0; i < g1::g_dspCount; ++i)
	{
		auto& d = mc.getDsp(i);
		std::printf("  DSP%u  irqd=%llu x:1=%06x  %llu/%llu/%llu ", i, static_cast<unsigned long long>(d.irqdCount()), d.dsp().memory().get(dsp56k::MemArea_X, 1), static_cast<unsigned long long>(d.hostWords()),
			static_cast<unsigned long long>(d.hostCommands()), static_cast<unsigned long long>(d.wordsToHost()));
		const auto& m = d.meter();
		for(uint32_t e = 0; e < 2; ++e)
			for(uint32_t sl = 0; sl < g1::Dsp::MeterSlots; ++sl)
				for(uint32_t l = 0; l < g1::Dsp::MeterLines; ++l)
					if(m[e][sl][l])
						std::printf(" [E%u s%u tx%u %06x]", e, sl, l, m[e][sl][l]);
		std::printf("  slots E0=%u E1=%u  HOTX queued=%zu  CPU accepts=%d  HSR=%06x HCR=%06x PC=$%06x\n", d.lastSlotCount(0), d.lastSlotCount(1),
			d.hdi08().txData().size(), mc.getHostPort(i).canReceiveData() ? 1 : 0, d.hdi08().readStatusRegister(), d.hdi08().readControlRegister(), d.dsp().getPC().toWord());
		std::printf("        SR=%06x OMR=%06x pending=%d  HC $7a masked=%d  HC $64=%d  IRQD=%d  IPRC=%06x IPRP=%06x  HCbusy=%d HCpend=%d\n",
			d.dsp().getSR().toWord(), d.dsp().regs().omr.var, d.dsp().hasPendingInterrupts() ? 1 : 0,
			d.dsp().isInterruptMasked(0x7a) ? 1 : 0, d.dsp().isInterruptMasked(0x64) ? 1 : 0, d.dsp().isInterruptMasked(0x16) ? 1 : 0,
			d.periph().read(0xffffff, dsp56k::Instruction::Invalid), d.periph().read(0xfffffe, dsp56k::Instruction::Invalid),
			d.hdi08().hostCommandBusy() ? 1 : 0, d.hdi08().hostCommandPending() ? 1 : 0);
		{
			auto rd = [&](dsp56k::TWord a) { return d.periph().read(a, dsp56k::Instruction::Invalid); };
			std::printf("        P:17=%06x X:FFF3=%06x TX0=%06x TX1=%06x  ESSI0 CRA=%06x CRB=%06x TSMA=%06x TSMB=%06x SSISR=%06x | ESSI1 CRA=%06x CRB=%06x TSMA=%06x | Y:6C0..6C3=%06x %06x %06x %06x  Y:6E0..6E3=%06x %06x %06x %06x\n",
				d.dsp().memory().get(dsp56k::MemArea_P, 0x17), rd(0xfffff3), rd(0xffffbc), rd(0xffffac), rd(0xffffb5), rd(0xffffb6), rd(0xffffb4), rd(0xffffb3), rd(0xffffb7), rd(0xffffa5), rd(0xffffa6), rd(0xffffa4),
				d.dsp().memory().get(dsp56k::MemArea_Y, 0x6c0), d.dsp().memory().get(dsp56k::MemArea_Y, 0x6c1), d.dsp().memory().get(dsp56k::MemArea_Y, 0x6c2), d.dsp().memory().get(dsp56k::MemArea_Y, 0x6c3),
				d.dsp().memory().get(dsp56k::MemArea_Y, 0x6e0), d.dsp().memory().get(dsp56k::MemArea_Y, 0x6e1), d.dsp().memory().get(dsp56k::MemArea_Y, 0x6e2), d.dsp().memory().get(dsp56k::MemArea_Y, 0x6e3));
		}
		std::printf("        PCs:");
		for(auto& [a, n] : d.pcWatch()) std::printf(" $%03x=%llu", a, static_cast<unsigned long long>(n));
		std::printf("\n");
		std::printf("        serviced vectors (last $%02x):", d.lastVector());
		for(auto& [v, n] : d.servicedVectors()) std::printf(" $%02x=%llu", v, static_cast<unsigned long long>(n));
		std::printf("\n");
		std::printf("        mode=%d SP=%06x LA=%06x LC=%06x ext.pend=%d\n", static_cast<int>(d.dsp().getProcessingMode()),
			d.dsp().regs().sp.var, d.dsp().regs().la.var, d.dsp().regs().lc.var, d.dsp().hasPendingExternalInterrupts() ? 1 : 0);
		auto& mem = d.dsp().memory();
		std::printf("        X:$6C0..$6CF:");
		for(dsp56k::TWord a = 0x6c0; a < 0x6d0; ++a) std::printf(" %06x", mem.get(dsp56k::MemArea_X, a));
		std::printf("\n        Y:$6C0..$6CF:");
		for(dsp56k::TWord a = 0x6c0; a < 0x6d0; ++a) std::printf(" %06x", mem.get(dsp56k::MemArea_Y, a));
		// how many non-zero words there are in internal X/Y (a sign that something is computed)
		uint32_t nzX = 0, nzY = 0;
		for(dsp56k::TWord a = 0; a < 0x800; ++a) { nzX += mem.get(dsp56k::MemArea_X, a) != 0; nzY += mem.get(dsp56k::MemArea_Y, a) != 0; }
		std::printf("\n        X:$6E0..$6EF:");
		for(dsp56k::TWord a = 0x6e0; a < 0x6f0; ++a) std::printf(" %06x", mem.get(dsp56k::MemArea_X, a));
		std::printf("\n        Y:$6E0..$6EF:");
		for(dsp56k::TWord a = 0x6e0; a < 0x6f0; ++a) std::printf(" %06x", mem.get(dsp56k::MemArea_Y, a));
		std::printf("\n        DMA DSTR=%06x", d.periph().getDMA().getDSTR());
		for(dsp56k::TWord c = 0; c < 6; ++c)
			std::printf("  c%u:DCR=%06x DSR=%06x DDR=%06x DCO=%06x", c, d.periph().getDMA().getDCR(c), d.periph().getDMA().getDSR(c), d.periph().getDMA().getDDR(c), d.periph().getDMA().getDCO(c));
		std::printf("\n        non-zero internal X/Y: %u / %u   PC=$%06x SR=%06x  X:5F=%06x Y:5F=%06x (on DSP 3: offset and volume)\n", nzX, nzY, d.dsp().getPC().toWord(), d.dsp().getSR().toWord(), mem.get(dsp56k::MemArea_X, 0x5f), mem.get(dsp56k::MemArea_Y, 0x5f));
	}
	for(uint32_t i = 0; i < g1::g_dspCount; ++i)
	{
		auto& d = mc.getDsp(i);
		std::printf("  DSP%u  booted=%d times=%u stalls=%llu frames=%llu  PC=$%06x  cycles=%llu  HCR=%06x HSR=%06x\n", i, d.booted(), d.bootCount(),
			static_cast<unsigned long long>(d.stalls()), static_cast<unsigned long long>(d.audioFrames()),
			d.dsp().getPC().toWord(), static_cast<unsigned long long>(d.dsp().getCycles()),
			d.hdi08().readControlRegister(), d.hdi08().readStatusRegister());
	}

	if(!watch.empty())
	{
		std::printf("\nwatch:");
		for(auto& [a, n] : watch) std::printf(" $%06x=%u", a, n);
		std::printf("\n");
	}

	std::printf("ADC multiplexer codes ($14420A):");
	for(uint32_t k = 0; k < 20; ++k) std::printf(" %02x", mc.read8(0x14420a + k));
	std::printf("\n");
	std::printf("voice lists per DSP ($1A84A8 + n*$602): ");
	for(uint32_t n = 0; n < 4; ++n)
	{
		const uint32_t b = 0x1a84a8 + n * 0x602;
		std::printf(" DSP%u[", n);
		for(uint32_t k = 0; k < 14; ++k) std::printf("%02x ", mc.read8(b + k));
		std::printf("]");
	}
	std::printf("\n");
	std::printf("PIT: %llu interrupts. CPU IPL mask (samples):", static_cast<unsigned long long>(mc.pitIrqs()));
	for(int k = 0; k < 8; ++k) std::printf(" %d:%llu", k, static_cast<unsigned long long>(iplHist[k]));
	std::printf("\n");
	{
		const uint32_t vbr = 0x1ab4e0;
		auto vec = [&](uint32_t v) { return (static_cast<uint32_t>(mc.read16(vbr + v * 4)) << 16) | mc.read16(vbr + v * 4 + 2); };
		std::printf("PIT pending at the end: %d   SR=%04x\n", mc.hasPendingInterrupt(0x40, 1) ? 1 : 0, mc.getSR());
		std::printf("vectors (VBR=$%06x): $40=$%06x $42=$%06x $55=$%06x $5A=$%06x  PICR=%04x PITR=%04x\n", vbr, vec(0x40), vec(0x42), vec(0x55), vec(0x5a),
			mc.read16(0xfffa22), mc.read16(0xfffa24));
	}
	std::printf("GPT: TCNT=%04x TOC1=%04x TOC2=%04x TMSK=%04x TFLG=%04x ICR=%04x MCR=%04x\n",
		mc.read16(0xfff90a), mc.read16(0xfff914), mc.read16(0xfff916), mc.read16(0xfff920), mc.read16(0xfff922), mc.read16(0xfff904), mc.read16(0xfff900));

	printChipSelects(mc);
	std::printf("\nflash: %u bytes programmed, %u sectors erased\n", mc.getFlash().programmedBytes(), mc.getFlash().erasedSectors());

	std::printf("\naccesses to unknown hardware (%zu addresses, ROM writes: %u):\n",
		mc.unknownAccesses().size(), mc.romWrites());
	int shown = 0;
	for(auto& [addr, a] : mc.unknownAccesses())
	{
		if(shown++ >= 60) { std::printf("  ...\n"); break; }
		std::printf("  $%06x  reads=%-8u writes=%-8u last=%04x  first PC=$%06x\n", addr, a.reads, a.writes, a.lastValue, a.firstPc);
	}
	return 0;
}
