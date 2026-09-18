// g1boot: arranca el OS del G1 en el 68331 emulado y cuenta lo que pasa.
//
//   g1boot ROM [millones_de_instrucciones]
//   g1boot ROM dis DESDE HASTA        (desensamblado, direcciones en hex)
//
// Imprime por donde va el PC, en que bucles se queda, los chip-selects que
// programa el arranque (= el mapa de memoria real) y los accesos a hardware que
// todavia no emulamos. La ROM es la del usuario y nunca entra en el repo.

#include "g1Lib/g1mc.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

namespace
{
	// Chip-selects del SIM del 68331: CSBARBT/CSORBT y CSBAR0..10/CSOR0..10.
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
			std::printf("  CS%-4s base=$%06x tam=%-4s bytes=%d %s dsack=%d\n", i < 0 ? "BOOT" : std::to_string(i).c_str(),
				base, sizes[bar & 7], mode, rw[(orr >> 11) & 3], (orr >> 6) & 0xf);
		}
	}
}

int main(int argc, char** argv)
{
	if(argc < 2)
	{
		std::fprintf(stderr, "uso: g1boot ROM [millones_de_instrucciones]\n");
		return 2;
	}
	std::ifstream f(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if(rom.size() != g1::g_romSize)
	{
		std::fprintf(stderr, "la ROM debe medir 512 KB (mide %zu)\n", rom.size());
		return 1;
	}
	g1::Microcontroller mc(rom);

	// g1boot ROM dis DESDE HASTA: desensambla (direcciones en hex)
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

	// Historial de lo que sale por el ESSI0 de cada DSP (valores distintos, min, max)
	struct Tap { int32_t mn = 0x7fffffff, mx = -0x7fffffff; std::map<int32_t, uint64_t> values; uint64_t n = 0; };
	std::array<std::array<Tap, 2>, g1::g_dspCount> taps;
	for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		mc.getDsp(d).setAudioCallback([&taps, d](const uint32_t _essi, const int32_t _l, const int32_t)
		{
			auto& t = taps[d][_essi];
			t.mn = std::min(t.mn, _l); t.mx = std::max(t.mx, _l); ++t.n;
			if(t.values.size() < 64) ++t.values[_l];
		});
	std::printf("reset: PC=$%06x SP=$%06x\n", mc.getPC(), mc.getAReg(7));
	std::fflush(stdout);

	std::array<uint32_t, 64> lastPcs{};	// ultimos PCs, para ver como se llega a un fallo
	size_t lastPcPos = 0;
	std::map<uint32_t, uint64_t> pcHits;	// PC -> veces, para ver en que bucle se queda
	uint64_t cycles = 0;
	uint32_t lastReport = 0;
	// A mitad de la ejecucion se manda el saludo de NME (IAm, version 3.3) por la UART
	// y se apunta todo lo que el OS saque por ella.
	std::vector<uint8_t> sciOut;
	bool iamSent = false;
	// g1boot ROM N replay FICHERO: a mitad de la ejecucion se mete por el PC Port todo lo
	// que NME mando en una sesion (lo graba g1run en pcport-in.bin).
	std::vector<uint8_t> replay;
	if(argc > 4 && std::string(argv[3]) == "replay")
	{
		std::ifstream rf(argv[4], std::ios::binary);
		replay.assign(std::istreambuf_iterator<char>(rf), std::istreambuf_iterator<char>());
		iamSent = true;	// el replay ya trae su IAm
	}
	for(uint64_t i = 0; i < steps; ++i)
	{
		if(!iamSent && i >= steps / 2)
		{
			iamSent = true;
			mc.getPcPort().receive({0xf0, 0x33, 0x00, 0x06, 0x00, 0x03, 0x03, 0xf7});
			std::printf("  >> IAm enviado por el PC PORT en la instruccion %llu\n", static_cast<unsigned long long>(i));
		}
		if(!replay.empty() && i == steps / 2)
		{
			mc.getSci().write({0x90, 60, 100});	// nota mantenida por el MIDI IN, canal 1
			std::printf("  >> nota 60 on por MIDI IN\n");
			for(uint32_t d = 0; d < g1::g_dspCount; ++d)
				for(auto& t : taps[d]) t = {};
		}
		if(!replay.empty() && i == steps / 4)
		{
			mc.getPcPort().receive(replay);
			std::printf("  >> replay: %zu bytes por el PC Port\n", replay.size());
		}
		if((i & 0xfff) == 0)
			mc.getSci().read(sciOut);
		const auto pc = mc.getPC();
		if(i > steps / 2)
			++pcHits[pc];
		if(pc >= g1::g_memSize || (mc.readImm16(pc) == 0 && mc.readImm16(pc + 2) == 0))
		{
			if(pc < g1::g_memSize)
				std::printf("PC en memoria vacia (opcode 0000): $%06x\n", pc);
			std::printf("PC fuera de ROM/RAM: $%06x tras %llu instrucciones. Ultimos PCs:\n", pc, static_cast<unsigned long long>(i));
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
		cycles += mc.exec();
		const auto report = static_cast<uint32_t>(i / (steps / 10));
		if(report != lastReport)
		{
			lastReport = report;
			std::printf("  %3u%%  PC=$%06x  ciclos=%llu  DSP:", report * 10, mc.getPC(), static_cast<unsigned long long>(cycles));
			for(uint32_t d = 0; d < g1::g_dspCount; ++d)
				std::printf(" %u:%s/%06x", d, mc.getDsp(d).booted() ? "on" : "boot", mc.getDsp(d).dsp().getPC().toWord());
			std::printf("\n");
			std::fflush(stdout);
		}
	}

	mc.getSci().read(sciOut);
	std::printf("\nQSM: QMCR=%04x QILR/QIVR=%04x SCCR0=%04x SCCR1=%04x SCSR=%04x  PORTQS/PQSPAR=%04x DDRQS=%04x\n",
		mc.read16(0xfffc00), mc.read16(0xfffc04), mc.read16(0xfffc08), mc.read16(0xfffc0a), mc.read16(0xfffc0c),
		mc.read16(0xfffc14), mc.read16(0xfffc16));
	std::printf("SIM: SIMCR=%04x  PORTE=%02x DDRE=%02x PEPAR=%02x  PORTF=%02x DDRF=%02x PFPAR=%02x  GPT: TMSK=%04x TFLG=%04x TCTL=%04x\n",
		mc.read16(0xfffa00), mc.read8(0xfffa11), mc.read8(0xfffa15), mc.read8(0xfffa17),
		mc.read8(0xfffa19), mc.read8(0xfffa1d), mc.read8(0xfffa1f),
		mc.read16(0xfff920), mc.read16(0xfff922), mc.read16(0xfff91e));
	std::printf("SCDR: %u lecturas, %u escrituras de la CPU\n", mc.sciDataReads(), mc.sciDataWrites());
	std::vector<uint8_t> pcOut;
	mc.getPcPort().takeTx(pcOut);
	auto& pc = mc.getPcPort();
	std::printf("PC PORT: %u interrupciones, %u bytes leidos por el OS, %u enviados; escrituras MR=%u CSR=%u CR=%u THR=%u ACR=%u IMR=%u\n",
		mc.pcPortIrqs(), pc.rxCount(), pc.txCount(), pc.writes(0), pc.writes(1), pc.writes(2), pc.writes(3), pc.writes(4), pc.writes(5));
	std::printf("PC PORT -> fuera (%zu bytes):", pcOut.size());
	for(size_t k = 0; k < std::min<size_t>(pcOut.size(), 300); ++k)
		std::printf(" %02x", pcOut[k]);
	std::printf("\n");
	std::printf("SCI -> fuera (%zu bytes):", sciOut.size());
	for(size_t k = 0; k < std::min<size_t>(sciOut.size(), 300); ++k)
		std::printf(" %02x", sciOut[k]);
	std::printf("\n");

	std::vector<std::pair<uint64_t, uint32_t>> hot;
	for(auto& [pc, n] : pcHits) hot.push_back({n, pc});
	std::sort(hot.rbegin(), hot.rend());
	std::printf("\nsegunda mitad, PCs mas repetidos (el bucle donde se queda):\n");
	for(size_t i = 0; i < std::min<size_t>(12, hot.size()); ++i)
	{
		char dis[128];
		mc.disassemble(hot[i].second, dis);
		std::printf("  $%06x  %10llu  %s\n", hot[i].second, static_cast<unsigned long long>(hot[i].first), dis);
	}

	// Traza HI08: por cada puerto, las primeras escrituras (palabras de 24 bits reconstruidas)
	{
		std::map<uint32_t, std::vector<std::string>> perPort;
		std::map<uint32_t, uint32_t> pendingHigh;
		for(const auto& a : mc.hostTrace())
		{
			const auto port = a.addr & ~7u;
			auto& v = perPort[port];
			if(v.size() >= 40 && !(port == 0x200018 && v.size() < 140)) continue;
			char buf[64];
			const auto reg = a.addr & 7;
			if(!a.write) { std::snprintf(buf, sizeof(buf), "r%u", reg); if(v.empty() || v.back() != buf) v.push_back(buf); continue; }
			if(reg == 4) { pendingHigh[port] = a.value; continue; }
			if(reg == 6) std::snprintf(buf, sizeof(buf), "TX:%06x", ((pendingHigh[port] & 0xff) << 16) | (a.value & 0xffff));
			else std::snprintf(buf, sizeof(buf), "w%u=%02x", reg, a.value & 0xff);
			v.push_back(buf);
		}
		std::printf("\ntraza HI08 (primeros eventos por puerto):\n");
		for(auto& [port, v] : perPort)
		{
			std::printf("  $%06x:", port);
			for(auto& e : v) std::printf(" %s", e.c_str());
			std::printf("\n");
		}
	}

	// Tablas de punteros a los puertos HI08 que usa el OS (RAM)
	std::printf("\npunteros en RAM:");
	for(uint32_t a : {0x15bd60u, 0x15bd70u, 0x15bd80u, 0x15bd90u, 0x144640u})
	{
		std::printf("\n  $%06x:", a);
		for(uint32_t i = 0; i < 16; i += 4)
			std::printf(" %08x", (static_cast<uint32_t>(mc.read16(a + i)) << 16) | mc.read16(a + i + 2));
	}
	std::printf("\n  num DSP ($1ab91c) = %u\n", mc.read8(0x1ab91c));

	// Volcado de la memoria de programa de cada DSP (para desensamblar con dspdis)
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

	std::printf("\nSalida ESSI (slot 0, TX0) por DSP:\n");
	for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		for(uint32_t e = 0; e < 2; ++e)
		{
			auto& t = taps[d][e];
			std::printf("  DSP%u E%u: %llu tramas, min %d max %d, %zu valores distintos:", d, e, static_cast<unsigned long long>(t.n), t.mn, t.mx, t.values.size());
			int k = 0;
			for(auto& [v, c] : t.values) { if(k++ >= 8) break; std::printf(" %06x(x%llu)", v & 0xffffff, static_cast<unsigned long long>(c)); }
			std::printf("\n");
		}

	std::printf("\nDSP (palabras/HC/respuestas por HI08, picos de audio):\n");
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
		std::printf("  slots E0=%u E1=%u\n", d.lastSlotCount(0), d.lastSlotCount(1));
		auto& mem = d.dsp().memory();
		std::printf("        X:$6C0..$6CF:");
		for(dsp56k::TWord a = 0x6c0; a < 0x6d0; ++a) std::printf(" %06x", mem.get(dsp56k::MemArea_X, a));
		std::printf("\n        Y:$6C0..$6CF:");
		for(dsp56k::TWord a = 0x6c0; a < 0x6d0; ++a) std::printf(" %06x", mem.get(dsp56k::MemArea_Y, a));
		// cuantas palabras distintas de cero hay en X/Y internas (señal de que se calcula algo)
		uint32_t nzX = 0, nzY = 0;
		for(dsp56k::TWord a = 0; a < 0x800; ++a) { nzX += mem.get(dsp56k::MemArea_X, a) != 0; nzY += mem.get(dsp56k::MemArea_Y, a) != 0; }
		std::printf("\n        X:$6E0..$6EF:");
		for(dsp56k::TWord a = 0x6e0; a < 0x6f0; ++a) std::printf(" %06x", mem.get(dsp56k::MemArea_X, a));
		std::printf("\n        Y:$6E0..$6EF:");
		for(dsp56k::TWord a = 0x6e0; a < 0x6f0; ++a) std::printf(" %06x", mem.get(dsp56k::MemArea_Y, a));
		std::printf("\n        DMA DSTR=%06x", d.periph().getDMA().getDSTR());
		for(dsp56k::TWord c = 0; c < 6; ++c)
			std::printf("  c%u:DCR=%06x DSR=%06x DDR=%06x DCO=%06x", c, d.periph().getDMA().getDCR(c), d.periph().getDMA().getDSR(c), d.periph().getDMA().getDDR(c), d.periph().getDMA().getDCO(c));
		std::printf("\n        X/Y internas no nulas: %u / %u   PC=$%06x SR=%06x\n", nzX, nzY, d.dsp().getPC().toWord(), d.dsp().getSR().toWord());
	}
	for(uint32_t i = 0; i < g1::g_dspCount; ++i)
	{
		auto& d = mc.getDsp(i);
		std::printf("  DSP%u  arrancado=%d veces=%u paradas=%llu tramas=%llu  PC=$%06x  ciclos=%llu  HCR=%06x HSR=%06x\n", i, d.booted(), d.bootCount(),
			static_cast<unsigned long long>(d.stalls()), static_cast<unsigned long long>(d.audioFrames()),
			d.dsp().getPC().toWord(), static_cast<unsigned long long>(d.dsp().getCycles()),
			d.hdi08().readControlRegister(), d.hdi08().readStatusRegister());
	}

	printChipSelects(mc);
	std::printf("\nflash: %u bytes programados, %u sectores borrados\n", mc.getFlash().programmedBytes(), mc.getFlash().erasedSectors());

	std::printf("\naccesos a hardware desconocido (%zu direcciones, escrituras a ROM: %u):\n",
		mc.unknownAccesses().size(), mc.romWrites());
	int shown = 0;
	for(auto& [addr, a] : mc.unknownAccesses())
	{
		if(shown++ >= 60) { std::printf("  ...\n"); break; }
		std::printf("  $%06x  lect=%-8u escr=%-8u ultimo=%04x  primer PC=$%06x\n", addr, a.reads, a.writes, a.lastValue, a.firstPc);
	}
	return 0;
}
