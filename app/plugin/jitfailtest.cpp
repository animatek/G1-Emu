// g1jitfailtest: the plugin's Runner when the DSP JIT cannot generate a block (issue #17).
//
//   g1jitfailtest recover   one block fails (G1_JIT_FAIL_AT): the DSP empties its JIT cache, goes
//                           on, and the patch still sounds; no DSP is given up.
//   g1jitfailtest fail      every block from early on fails (G1_JIT_FAIL_FROM): each DSP retries
//                           once and is then given up. The host must keep getting its blocks, in
//                           silence and on time, with nothing hanging and nothing crashing.
//
// The failure is simulated through the variables the core overlay reads (cmake/Dsp56300.cmake),
// set here before the first block is emitted, so each mode is a process of its own. The patch is
// SimpleOSC, which drones with no note, put in slot A by a SlotKeeper restore. Exit code 77
// (skipped) without a ROM, 1 on any failure.

#include "runner.h"

#include "romfinder.h"
#include "slotkeeper.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
	void setEnv(const char* _name, const char* _value)
	{
#ifdef _WIN32
		_putenv_s(_name, _value);
#else
		setenv(_name, _value, 1);
#endif
	}

	const std::vector<std::vector<uint8_t>> g_simpleOsc = {
		{0x37, 0x00, 0x00, 0x00, 0x53, 0x69, 0x6d, 0x70, 0x6c, 0x65, 0x4f, 0x53, 0x43, 0x00},
		{0x21, 0x01, 0xfc, 0x07, 0xf1, 0x00, 0x40, 0x7d, 0x02, 0xfe, 0x78},
		{0x4a, 0x82, 0x0e, 0x04, 0x10, 0x30, 0x80, 0x81, 0x09}, {0x4a, 0x00},
		{0x69, 0x80, 0x00, 0x00, 0x20, 0x00, 0x00},
		{0x52, 0x80, 0x02, 0x00, 0x40, 0x82, 0x00, 0x01, 0x02, 0x08, 0x10}, {0x52, 0x00, 0x00},
		{0x4d, 0x82, 0x02, 0x1e, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x02, 0x09, 0x90, 0x00}, {0x4d, 0x00},
		{0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x62, 0xc0, 0x40, 0x60, 0x20, 0x40, 0x00, 0x00}, {0x60, 0x00},
		{0x5b, 0x80}, {0x5b, 0x00},
		{0x5a, 0x82, 0x01, 0x4f, 0x73, 0x63, 0x41, 0x00, 0x02, 0x32, 0x4f, 0x75, 0x74, 0x70, 0x75, 0x74, 0x00}, {0x5a, 0x00}};
}

int main(int _argc, char** _argv)
{
	const std::string mode = _argc > 1 ? _argv[1] : "";
	if(mode == "recover")
		setEnv("G1_JIT_FAIL_AT", "200");
	else if(mode == "fail")
		setEnv("G1_JIT_FAIL_FROM", "100");
	else
	{
		std::printf("usage: g1jitfailtest recover|fail\n");
		return 2;
	}

	g1app::HostOptions options;
	options.load(g1app::defaultSettingsPath());
	// The settings give the ROM, not the model: one left on the Micro would hand a 4-DSP G1 the Micro's
	// OS, which then waits for ever. G1_MODEL still picks one.
	options.expanded = options.micro = false;
	if(const char* v = std::getenv("G1_ROM"))
		options.rom = v;
	const auto search = g1app::findRom({}, options.rom);
	std::vector<uint8_t> rom;
	if(!search.found() || !g1app::inspectRom(search.path, rom).ok())
	{
		std::printf("skipped: no ROM\n");
		return 77;
	}

	std::string osNote;
	g1app::Engine engine(rom, options.loadOs(osNote));
	if(!osNote.empty())
		std::printf("%s\n", osNote.c_str());
	g1app::SlotKeeper keeper;
	g1app::SlotKeeper::Slots slots;
	slots[0].sections = g_simpleOsc;
	keeper.restore(slots);

	constexpr double rate = 48000.0;
	constexpr size_t block = 512;
	constexpr double seconds = 8.0;
	float peakLast2s = 0;
	const auto t0 = std::chrono::steady_clock::now();
	{
		g1app::Runner runner(engine, rate, block, 36.0f, nullptr, 0, &keeper);
		std::vector<float> out[4];
		for(auto& o : out)
			o.resize(block);
		float* outs[4] = {out[0].data(), out[1].data(), out[2].data(), out[3].data()};
		const auto total = static_cast<size_t>(seconds * rate);
		for(size_t pos = 0; pos < total; pos += block)
		{
			runner.process(outs, 4, nullptr, 0, block, true);
			if(pos >= static_cast<size_t>((seconds - 2.0) * rate))
				for(size_t i = 0; i < block; ++i)
					peakLast2s = std::max(peakLast2s, std::abs(out[0][i]));
			// Offline, a block that never comes would hold this loop for the runner's 10 s limit.
			if(std::chrono::steady_clock::now() - t0 > std::chrono::seconds(120))
			{
				std::printf("FAIL: the render does not get anywhere (%.1f s of %.0f in 120 s)\n", pos / rate, seconds);
				return 1;
			}
		}
	}
	const double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

	auto& mc = engine.mc();
	uint32_t failed = 0, recovered = 0;
	for(uint32_t d = 0; d < mc.dspCount(); ++d)
	{
		const auto& dsp = mc.getDsp(d);
		failed += dsp.jitFailed();
		recovered += dsp.jitRecoveries() > 0;
		std::printf("DSP %u: %s, %u retr%s%s%s\n", d, dsp.jitFailed() ? "given up" : "running", dsp.jitRecoveries(),
			dsp.jitRecoveries() == 1 ? "y" : "ies", dsp.jitFailed() ? ": " : "", dsp.jitFailure().c_str());
	}
	const double db = peakLast2s > 0 ? 20.0 * std::log10(peakLast2s) : -200.0;
	std::printf("%.0f s rendered in %.1f s; output 1 in the last 2 s: %.1f dBFS\n", seconds, wall, db);

	bool ok;
	if(mode == "recover")
		ok = recovered >= 1 && failed == 0 && peakLast2s > 1e-3f;
	else
		ok = failed == mc.dspCount() && peakLast2s == 0.0f;
	std::printf("%s\n", ok ? "passed" : "FAILED");
	return ok ? 0 : 1;
}
