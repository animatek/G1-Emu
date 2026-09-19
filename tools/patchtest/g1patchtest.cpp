// g1patchtest: banco de pruebas del G1 emulado, sin ventana ni editor.
//
//   g1patchtest ROM patch.pch [--note 60] [--seconds 2] [--wav salida.wav]
//
// Arranca el OS, saluda como NME (IAm), sube el patch con el mismo codigo que usa NME
// (PchFileIO -> PatchSerializer -> UploadPacketizer), paquete a paquete esperando cada
// respuesta, toca una nota por el PC Port y mide las cuatro salidas y los enlaces entre DSP.
// Sirve para probar modulo a modulo que suena y que no, sin tocar el G1 de nadie.
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
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	constexpr uint64_t g_ms = g1::g_ucClock / 1000;
	constexpr int32_t g_silence = 0x155;	// X:$5F del DSP 3: lo que sale sin senal

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

	// Manda un mensaje por el PC Port y corre hasta que el OS conteste algo (o pase _timeoutMs).
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
				run(_mc, 5 * g_ms);	// por si llega algo mas detras
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

	// Frecuencia dominante por cruces por cero (vale para comprobar la nota, no para espectros).
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
		std::fprintf(stderr, "uso: g1patchtest ROM patch.pch [--note N] [--seconds S] [--wav fichero.wav] [--input-sine Hz]\n");
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

	// El patch, con las descripciones de modulos de NME.
	ModuleDescriptions descs;
	if(!descs.loadFromFile(juce::File(NME_DATA_DIR).getChildFile("modules.xml")))
	{
		std::fprintf(stderr, "no puedo cargar %s/modules.xml\n", NME_DATA_DIR);
		return 1;
	}
	PchFileIO io(descs);
	auto patch = io.readFile(juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2])));
	if(!patch)
	{
		std::fprintf(stderr, "no puedo leer el patch %s\n", argv[2]);
		return 1;
	}
	PatchSerializer serializer;
	const auto packets = UploadPacketizer::cut(serializer.serializeForUpload(*patch));

	std::ifstream f(argv[1], std::ios::binary);
	std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if(rom.size() != g1::g_romSize)
	{
		std::fprintf(stderr, "la ROM debe medir 512 KB\n");
		return 1;
	}
	g1::Microcontroller mc(rom);
	mc.installRomOsInFlash();

	// Salida: una muestra por bloque del DSP 3 (4 canales).
	std::vector<std::array<int32_t, 4>> blocks;
	bool capture = false;
	mc.getDsp(g1::g_dspCount - 1).setBlockCallback([&](const int32_t _a, const int32_t _b, const int32_t _c, const int32_t _d)
	{
		if(capture)
			blocks.push_back({_a, _b, _c, _d});
	});

	// --input-sine F: por las entradas de audio entra un seno de F Hz (L) y de 2F Hz (R), a -12 dBFS.
	uint64_t inputPhase = 0;
	if(inputHz > 0)
		mc.getDsp(0).setInputProvider([&](int32_t& _l, int32_t& _r)
		{
			const double t = static_cast<double>(inputPhase++) / 96000.0;
			_l = static_cast<int32_t>(0.25 * 8388607.0 * std::sin(2.0 * M_PI * inputHz * t));
			_r = static_cast<int32_t>(0.25 * 8388607.0 * std::sin(2.0 * M_PI * 2.0 * inputHz * t));
		});

	// Arranque y saludo.
	run(mc, 1500 * g_ms);
	std::vector<uint8_t> boot;
	mc.getPcPort().takeTx(boot);
	const auto hello = transact(mc, {0xf0, 0x33, 0x00, 0x06, 0x00, 0x03, 0x03, 0xf7}, 1000);
	std::printf("IAm -> %s\n", hex(hello).c_str());
	auto showLcd = [&](const char* _when)
	{
		const auto& lcd = mc.getLcd();
		std::printf("pantalla (%s, %llu escrituras):\n  |%s|\n  |%s|\n", _when, static_cast<unsigned long long>(lcd.writes()), lcd.line(0, 20).c_str(), lcd.line(1, 20).c_str());
	};
	showLcd("al arrancar");
	if(hello.empty())
	{
		std::printf("el OS no contesta al saludo\n");
		return 1;
	}

	// Lo que NME manda al conectar, antes de subir nada (sacado de una sesion grabada: pide el
	// estado de los slots, la lista de patches, etc.). Pid 0 = sin patch cargado todavia.
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

	// Subida, como NME: un paquete, su respuesta, el siguiente.
	int pid = -1;
	for(size_t i = 0; i < packets.size(); ++i)
	{
		const auto msg = UploadPacketizer::frame(packets[i], i == 0, i + 1 == packets.size(), 0);
		const auto reply = transact(mc, msg);
		if(std::getenv("G1_VERBOSE"))
			std::printf("  paquete %zu: %s -> %s\n", i + 1, hex(msg, 12).c_str(), hex(reply, 16).c_str());
		// ACK del slot 0: F0 33 58 06 xx 36 pid ...
		for(size_t k = 0; k + 6 < reply.size(); ++k)
			if(reply[k] == 0xf0 && reply[k + 1] == 0x33 && (reply[k + 2] >> 2) == 0x16 && reply[k + 5] == 0x36)
				pid = reply[k + 6];
		if(reply.empty())
			std::printf("  paquete %zu/%zu: SIN RESPUESTA\n", i + 1, packets.size());
	}
	std::printf("patch \"%s\" subido en %zu paquetes; pid=%d\n", patch->getName().toRawUTF8(), packets.size(), pid);
	if(pid < 0)
	{
		std::printf("el OS no ha confirmado el patch\n");
		return 1;
	}
	run(mc, 300 * g_ms);	// que el OS cargue los DSP

	// Asignaciones de mandos, como hace NME despues de subir: las del .pch y, con
	// G1_KNOBS="mando:modulo:param,...", otras (mando 0-17 = 1-18, seccion poly).
	std::vector<std::array<int, 4>> knobs;	// mando, seccion, modulo, parametro
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
			std::printf("  mando %d -> modulo %d param %d: %s\n", k[0], k[2], k[3], hex(reply, 16).c_str());
	}
	if(!knobs.empty())
		run(mc, 200 * g_ms);

	for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		mc.getDsp(d).resetLinkPeak();
	// G1_PCWATCH=174,194: cuantas veces pasa cada DSP por esas direcciones durante la nota
	// (solo cuenta al principio de un bloque del JIT: vale para bucles y saltos).
	std::vector<uint32_t> watch;
	if(const char* w = std::getenv("G1_PCWATCH"))
		for(const auto& t : juce::StringArray::fromTokens(w, ",", ""))
			watch.push_back(static_cast<uint32_t>(t.getHexValue32()));
	for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		for(const auto a : watch)
			mc.getDsp(d).pcWatch()[a] = 0;
	blocks.clear();
	capture = true;
	// Nota por el PC Port, como NME: cc $17, 56 00 nota (pulsar) ... 56 01 nota (soltar).
	const auto on = withChecksum({0xf0, 0x33, 0x5c, 0x06, static_cast<uint8_t>(pid), 0x56, 0x00, static_cast<uint8_t>(note)});
	mc.getPcPort().receive(on);
	run(mc, static_cast<uint64_t>(seconds * 1000) * g_ms);
	capture = false;
	std::vector<uint8_t> rest;
	mc.getPcPort().takeTx(rest);
	for(size_t k = 0; k + 5 < rest.size(); ++k)
		if(rest[k] == 0xf0 && rest[k + 5] == 0x7f)	// error del OS
			std::printf("el OS ha contestado un error: %s\n", hex({rest.begin() + static_cast<long>(k), rest.end()}).c_str());

	if(std::getenv("G1_VERBOSE"))
		for(uint32_t d = 0; d < g1::g_dspCount; ++d)
		{
			auto& p = mc.getDsp(d).periph();
			for(uint32_t ch = 2; ch < 4; ++ch)
				std::printf("DSP%u  DMA%u DCR=%06x DSR=%06x DDR=%06x DCO=%06x\n", d, ch, p.getDMA().getDCR(ch), p.getDMA().getDSR(ch), p.getDMA().getDDR(ch), p.getDMA().getDCO(ch));
			std::printf("DSP%u  ESSI0 SR=%06x CRB=%06x RX=%06x | ESSI1 SR=%06x CRB=%06x RX=%06x\n", d,
				static_cast<uint32_t>(p.getEssi0().getSR()), static_cast<uint32_t>(p.getEssi0().getCRB()), 0u,
				static_cast<uint32_t>(p.getEssi1().getSR()), static_cast<uint32_t>(p.getEssi1().getCRB()), 0u);
			std::printf("DSP%u  DOR0=%06x DOR1=%06x DCO1=%06x (leidos por el programa: X:$FFFFF3/F2/E9 = %06x %06x %06x)\n", d,
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
	showLcd("con la nota");
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
	std::printf("LEDs (filas 0-3): %s\n", leds().c_str());
	// Estado de los 32 LEDs mirando 20 veces en 1 s: '#' encendido, '.' apagado, '*' parpadea.
	// Fila 0 a 3, bit 7 a 0 (encendido = bit a 0).
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
		std::printf("LEDs (fila 0..3, bit 7..0): %s\n", ledStates().c_str());
	// G1_PRESS=fila.bit: pulsa ese boton desde el estado de arranque y dice como quedan la pantalla
	// y los LEDs (antes y despues). Para casar los botones, uno por proceso.
	if(const char* pr = std::getenv("G1_PRESS"))
	{
		// Varios separados por comas: se pulsan en orden y se informa del ultimo.
		const auto seq = juce::StringArray::fromTokens(pr, ",", "");
		for(int k = 0; k + 1 < seq.size(); ++k)
		{
			const auto g = juce::StringArray::fromTokens(seq[k], ".", "");
			mc.setButton(static_cast<uint32_t>(g[0].getIntValue()), static_cast<uint32_t>(g[1].getIntValue()), true);
			run(mc, 150 * g_ms);
			mc.setButton(static_cast<uint32_t>(g[0].getIntValue()), static_cast<uint32_t>(g[1].getIntValue()), false);
			run(mc, 300 * g_ms);
		}
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
		std::printf("PULSA %s  pantalla %s -> %s  LEDs %s -> %s\n", pr, s0.c_str(), screen().c_str(), l0.c_str(), ledStates().c_str());
	}
	// G1_ADCSWEEP=1: sube cada canal del ADC de 0 a 200 y dice que manda el OS por el PC Port
	// (con mandos asignados, un Parameter con el modulo y el valor: asi se sabe que mando es).
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
					std::printf(" [seccion %u modulo %u param %u = %u]", outMsgs[k + 6], outMsgs[k + 7], outMsgs[k + 8], outMsgs[k + 9]);
			std::printf("  %s\n", hex(outMsgs, 30).c_str());
			mc.setAdc(code, 0);
			run(mc, 100 * g_ms);
		}
	// G1_PROBE=1: pulsa uno a uno los 24 botones de la matriz y dice que cambia en la pantalla y
	// en los LEDs. Sirve para saber que boton del panel es cada bit.
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
				std::printf("boton fila %u bit %u: %s%s%s%s\n", row, bit,
					l1 != l0 ? ("pantalla [" + l1 + "]") .c_str() : "", led1 != led0 ? (" LEDs " + led0 + "-> " + led1).c_str() : "",
					(l1 == l0 && led1 == led0) ? "sin cambios" : "", "");
			}
	// Informe: las cuatro salidas (sin el silencio $155) y los enlaces.
	std::printf("\nsalidas (%.2f s, %zu muestras a 96 kHz):\n", blocks.size() / 96000.0, blocks.size());
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
			std::printf("  salida %u: silencio\n", c + 1);
		else
			std::printf("  salida %u: pico %6.1f dBFS (%+.1f con los +36 dB de g1run), ~%.1f Hz\n", c + 1, db(peak), db(peak) + 36, zeroCrossHz(x, 96000));
	}
	std::printf("enlaces (pico por canal, dBFS; '.' = cero):\n");
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

	// G1_DUMP=carpeta: memoria P, X e Y de cada DSP al acabar (para desensamblar con dspdis).
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
		std::printf("WAV (4 canales, 96 kHz, +36 dB) en %s\n", wavPath.c_str());
	}
	return 0;
}
