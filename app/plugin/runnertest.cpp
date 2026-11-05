// g1runnertest: the plugin's Runner without a DAW.
//
//   g1runnertest [--realtime] [--burst K] [--seconds N] [--program N]
//
// G1_MODEL=expanded runs it on an Expanded (8 DSPs): the outputs then come from DSP 7.
//
// Boots a G1 from the ROM (found like the standalone finds it) with a copy of the standalone's
// flash when there is one, and plays note 60 on channel 1 from a fake host:
//
//   1. offline, blocks of 256 frames;
//   2. offline, blocks of random sizes from 1 to 512, same announced maximum and so same latency.
//      The note must land on the same G1 frame whatever the blocks: the two renders must be
//      identical, bit for bit.
//   3. with --realtime: blocks of 256 frames paced by the wall clock, as an audio callback would
//      be called, counting dropouts.
//   4. with --burst K: blocks of 512 called as a prefetch thread calls them (ASIO-Guard in Cubase
//      and Nuendo, REAPER's anticipative FX, #86): it keeps up to K + 1 blocks ready before they
//      play, so it rests and then calls K blocks back to back. Counts the dropouts, the blocks that
//      waited for the worker, and the blocks that were not ready when they were due to play.
//
// Exit code 77 (skipped) without a ROM, 1 if the renders differ or a dropout hides sound (one
// while the G1 is silent, as it is while it loads a patch, hides nothing and is only reported).

#include "runner.h"

#include "hostconfig.h"
#include "romfinder.h"

#include "g1Lib/g1thread.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/resource.h>
#endif

namespace
{
	// The CPU time of the whole process (all its threads), in seconds: how many cores the emulator
	// takes is this over the wall time.
	double processCpuSeconds()
	{
#ifdef _WIN32
		FILETIME created, exited, kernel, user;
		if(!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user))
			return 0;
		const auto toS = [](const FILETIME& _t) { return static_cast<double>((static_cast<uint64_t>(_t.dwHighDateTime) << 32) | _t.dwLowDateTime) * 1e-7; };
		return toS(kernel) + toS(user);
#else
		rusage u{};
		getrusage(RUSAGE_SELF, &u);
		return static_cast<double>(u.ru_utime.tv_sec + u.ru_stime.tv_sec) + static_cast<double>(u.ru_utime.tv_usec + u.ru_stime.tv_usec) * 1e-6;
#endif
	}

	constexpr double g_rate = 48000.0;
	constexpr size_t g_maxBlock = 512;
	int g_program = -1;		// --program N: a Program Change on channel 1 at 2 s, before the note

	struct Setup
	{
		std::vector<uint8_t> rom, flash, os;
	};

	std::unique_ptr<g1app::Engine> makeEngine(const Setup& _s)
	{
		auto e = std::make_unique<g1app::Engine>(_s.rom, _s.os, g1app::HostOptions().dspCount());
		if(!_s.flash.empty())
			e->loadFlash(_s.flash);
		return e;
	}

	struct Render
	{
		std::vector<float> out[4];
		uint64_t xruns = 0;
		double wallSeconds = 0;
		size_t latency = 0;
		double load = 0;
		double cores = 0;			// CPU time over wall time, the whole process
		std::string display;		// the G1's display at the end
		std::vector<double> xrunAt;	// when the dropouts were, in seconds
		uint64_t waits = 0;			// blocks that waited for the worker (--burst)
		uint64_t late = 0;			// blocks not ready when due to play (--burst)
	};

	// _blockSize 0: random sizes. _realtime: paced by the wall clock; with _burst, by a prefetch
	// thread that rests and then calls _burst blocks back to back.
	Render render(const Setup& _s, const double _seconds, const size_t _blockSize, const bool _realtime, const size_t _burst = 0)
	{
		auto engine = makeEngine(_s);
		g1app::Runner runner(*engine, g_rate, g_maxBlock, 36.0f);
		const auto total = static_cast<size_t>(_seconds * g_rate);
		const auto noteOn = static_cast<size_t>((_seconds - 3.0) * g_rate), noteOff = static_cast<size_t>((_seconds - 1.0) * g_rate);
		const auto programFrame = static_cast<size_t>((std::getenv("G1_TEST_PC_AT") ? std::atof(std::getenv("G1_TEST_PC_AT")) : 2.0) * g_rate);
		Render r;
		r.latency = runner.latency();
		for(auto& o : r.out)
			o.assign(total, 0.0f);

		uint32_t lcg = 12345;
		size_t pos = 0;
		const auto start = std::chrono::steady_clock::now();
		const auto cpuStart = processCpuSeconds();
		auto deadline = start;
		const auto blockTime = std::chrono::nanoseconds(static_cast<int64_t>(1e9 * static_cast<double>(_blockSize) / g_rate));
		const auto guard = blockTime * static_cast<int64_t>(_burst + 1);	// how far ahead the prefetch thread keeps
		const auto playStart = start + guard;
		auto playAt = [&](const size_t _pos) { return playStart + std::chrono::nanoseconds(static_cast<int64_t>(1e9 * static_cast<double>(_pos) / g_rate)); };
		while(pos < total)
		{
			size_t n = _blockSize;
			if(!n)
			{
				lcg = lcg * 1664525u + 1013904223u;
				n = 1 + (lcg >> 8) % g_maxBlock;
			}
			n = std::min(n, total - pos);

			if(g_program >= 0 && programFrame >= pos && programFrame < pos + n)
			{
				const uint8_t msg[2] = {0xc0, static_cast<uint8_t>(g_program)};
				runner.queueMidi(static_cast<uint32_t>(programFrame - pos), msg, 2);
			}
			for(const auto [frame, status, velocity] : {std::tuple{noteOn, uint8_t(0x90), uint8_t(100)}, std::tuple{noteOff, uint8_t(0x80), uint8_t(0)}})
				if(frame >= pos && frame < pos + n)
				{
					const uint8_t msg[3] = {status, 60, velocity};
					runner.queueMidi(static_cast<uint32_t>(frame - pos), msg, 3);
				}

			float* outs[4];
			for(size_t c = 0; c < 4; ++c)
				outs[c] = r.out[c].data() + pos;
			if(_realtime && _burst)
			{
				// Ready more than the guard ahead: rest until only two blocks are left, then go.
				if(playAt(pos) - std::chrono::steady_clock::now() > guard)
				{
					const auto wake = playAt(pos) - 2 * blockTime;
					const auto left = std::chrono::duration_cast<std::chrono::microseconds>(wake - std::chrono::steady_clock::now());
					if(left.count() > 600)
						g1::preciseSleep(left - std::chrono::microseconds(500));
					while(std::chrono::steady_clock::now() < wake)
						std::this_thread::yield();
				}
			}
			else if(_realtime)
			{
				deadline += std::chrono::nanoseconds(static_cast<int64_t>(1e9 * static_cast<double>(n) / g_rate));
				// As a sound card would call it: on time. sleep_until on Windows wakes up to 15.6 ms late,
				// which would ask the runner for several blocks at once.
				const auto left = std::chrono::duration_cast<std::chrono::microseconds>(deadline - std::chrono::steady_clock::now());
				if(left.count() > 600)
					g1::preciseSleep(left - std::chrono::microseconds(500));
				while(std::chrono::steady_clock::now() < deadline)
					std::this_thread::yield();
			}
			runner.process(outs, 4, nullptr, 0, n, !_realtime);
			if(_realtime && _burst && std::chrono::steady_clock::now() > playAt(pos))
				++r.late;
			if(_realtime)
			{
				const auto x = runner.stats().xruns;
				if(x != r.xruns)
					r.xrunAt.push_back(static_cast<double>(pos) / g_rate);
				r.xruns = x;
			}
			pos += n;
		}
		r.wallSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		r.cores = (processCpuSeconds() - cpuStart) / r.wallSeconds;
		{
			auto lock = runner.lockEngine();
			r.display = engine->mc().getLcd().line(0, 16) + " | " + engine->mc().getLcd().line(1, 16);
		}
		const auto s = runner.stats();
		r.xruns = s.xruns;
		r.load = s.load;
		r.waits = runner.waits();
		return r;
	}

	double dbfs(const std::vector<float>& _x, const size_t _from, const size_t _to)
	{
		double peak = 0;
		for(size_t i = _from; i < _to && i < _x.size(); ++i)
			peak = std::max(peak, static_cast<double>(std::fabs(_x[i])));
		return peak > 0 ? 20.0 * std::log10(peak) : -200.0;
	}

	size_t onset(const std::vector<float>& _x, const size_t _from)
	{
		for(size_t i = _from; i < _x.size(); ++i)
			if(std::fabs(_x[i]) > 1e-3f)
				return i;
		return 0;
	}
}

int main(int _argc, char** _argv)
{
	bool realtime = false;
	size_t burst = 0;
	double seconds = 8.0;
	for(int i = 1; i < _argc; ++i)
	{
		if(!std::strcmp(_argv[i], "--realtime"))
			realtime = true;
		else if(!std::strcmp(_argv[i], "--burst") && i + 1 < _argc)
			burst = static_cast<size_t>(std::max(1, std::atoi(_argv[++i])));
		else if(!std::strcmp(_argv[i], "--program") && i + 1 < _argc)
			g_program = std::atoi(_argv[++i]);
		else if(!std::strcmp(_argv[i], "--seconds") && i + 1 < _argc)
			seconds = std::max(4.0, std::atof(_argv[++i]));
	}

	g1app::HostOptions options;
	options.load(g1app::defaultSettingsPath());
	// The settings give the ROM, not the model: one left on the Micro would hand a 4-DSP G1 the Micro's
	// OS, which then waits for ever. G1_MODEL still picks one.
	options.expanded = options.micro = false;
	if(const char* v = std::getenv("G1_ROM"))
		options.rom = v;
	const auto search = g1app::findRom({}, options.rom);
	Setup setup;
	std::string osNote;
	setup.os = options.loadOs(osNote);
	if(!osNote.empty())
		std::printf("%s\n", osNote.c_str());
	if(!search.found() || !g1app::inspectRom(search.path, setup.rom).ok())
	{
		std::printf("skipped: no ROM\n");
		return 77;
	}
	{
		// The model's own flash, as the plugin takes it (Processor): the Micro's is flash-micro.bin. A
		// rack's flash in a Micro sends its OS (with the buttons released, as they are now) into
		// sorting out a flash that is not its own, and the test then never ends.
		auto flashPath = std::filesystem::path(g1app::defaultFlashPath());
		if(g1app::HostOptions().dspCount() == g1::g_microDsps)
			flashPath = flashPath.parent_path() / "flash-micro.bin";
		std::ifstream f(flashPath, std::ios::binary);
		setup.flash.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
		if(setup.flash.size() != g1::Flash::Size)
			setup.flash.clear();
	}
	std::printf("ROM %s, flash: %s\n", search.path.c_str(), setup.flash.empty() ? "factory (no patches: expect silence)" : "copy of the standalone's");

	const auto noteOn = static_cast<size_t>((seconds - 3.0) * g_rate);
	int failed = 0;

	const auto a = render(setup, seconds, 256, false);
	std::printf("offline, blocks of 256:    %.1f s of audio in %.1f s, latency %zu frames, before the note %.1f dBFS, after %.1f dBFS, onset at frame %zu (note sent at %zu)\n",
		seconds, a.wallSeconds, a.latency, dbfs(a.out[0], 0, noteOn), dbfs(a.out[0], noteOn, a.out[0].size()), onset(a.out[0], noteOn), noteOn);

	std::printf("  display: %s\n  peak per half second (dBFS):", a.display.c_str());
	for(size_t w = 0; w + 24000 <= a.out[0].size(); w += 24000)
		std::printf(" %.0f", dbfs(a.out[0], w, w + 24000));
	std::printf("\n");

	const auto b = render(setup, seconds, 0, false);
	size_t diffs = 0, first = 0;
	for(size_t c = 0; c < 4; ++c)
		for(size_t i = 0; i < a.out[c].size(); ++i)
			if(a.out[c][i] != b.out[c][i] && !diffs++)
				first = i;
	std::printf("offline, random blocks:    %.1f s of audio in %.1f s; %s", seconds, b.wallSeconds, diffs ? "DIFFERS from blocks of 256" : "identical to blocks of 256, bit for bit\n");
	if(diffs)
	{
		std::printf(" in %zu samples, first at frame %zu\n", diffs, first);
		failed = 1;
	}

	if(realtime)
	{
		const auto c = render(setup, seconds, 256, true);
		std::printf("real time, blocks of 256:  %llu dropouts, %llu blocks waited, worker load %.0f%%, %.2f cores, after the note %.1f dBFS\n",
			static_cast<unsigned long long>(c.xruns), static_cast<unsigned long long>(c.waits), c.load, c.cores, dbfs(c.out[0], noteOn, c.out[0].size()));
		std::printf("  peak per half second (dBFS):");
		for(size_t w = 0; w + 24000 <= c.out[0].size(); w += 24000)
			std::printf(" %.0f", dbfs(c.out[0], w, w + 24000));
		std::printf("\n");
		// A dropout while the G1 is silent anyway (loading a patch mutes it, and loading is when
		// the emulator runs slower than real time) hides nothing. One over sound is a failure.
		size_t audible = 0;
		for(const auto t : c.xrunAt)
		{
			const auto at = static_cast<size_t>(t * g_rate);
			if(dbfs(a.out[0], at, at + 256) > -120.0 || dbfs(a.out[1], at, at + 256) > -120.0)
				++audible;
		}
		// Slower than real time it may break up, but never go quiet: it did, for good, when every late
		// frame was owed and dropped (an Expanded on a machine that could not keep up).
		if(dbfs(a.out[0], noteOn, a.out[0].size()) > -120.0 && dbfs(c.out[0], noteOn, c.out[0].size()) <= -120.0)
		{
			std::printf("  SILENT in real time, with sound offline\n");
			failed = 1;
		}
		if(!c.xrunAt.empty())
			std::printf("  dropouts from %.2f s to %.2f s, %zu of them over sound\n", c.xrunAt.front(), c.xrunAt.back(), audible);
		if(audible)
			failed = 1;
	}
	if(burst)
	{
		const auto c = render(setup, seconds, 512, true, burst);
		size_t audible = 0;
		for(const auto t : c.xrunAt)
		{
			const auto at = static_cast<size_t>(t * g_rate);
			if(dbfs(a.out[0], at, at + 512) > -120.0 || dbfs(a.out[1], at, at + 512) > -120.0)
				++audible;
		}
		std::printf("prefetch, %zu blocks of 512 at a time: %llu dropouts (%zu over sound), %llu blocks waited, %llu late, worker load %.0f%%, after the note %.1f dBFS\n",
			burst, static_cast<unsigned long long>(c.xruns), audible, static_cast<unsigned long long>(c.waits),
			static_cast<unsigned long long>(c.late), c.load, dbfs(c.out[0], noteOn, c.out[0].size()));
		if(audible || c.late)
			failed = 1;
	}
	return failed;
}
