// jitdiff: runs DSP56300 instructions once on the JIT and once on the interpreter, from the same
// random state, and reports every register or memory word that ends up different. A disagreement
// is a JIT (or interpreter) bug in that instruction.
//
//   jitdiff [trials] < words.hex        JIT against interpreter, one instruction at a time
//   jitdiff [trials] K < words.hex      every run of K instructions as one JIT block against the
//                                       same JIT one instruction per block: finds state the JIT
//                                       carries wrongly between instructions (how CMPM was found)
//
// One instruction word per line; a two-word instruction takes the next line as its extension
// word. Flow the harness cannot contain (loops, calls, returns, absolute jumps, data words) is
// skipped. The interpreter is itself wrong on IFcc/Bcc with LE when Z = 1 and N != V, so those
// disagreements in the first mode are the interpreter's.
#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"
#include "dsp56kEmu/disasm.h"
#include "dsp56kEmu/opcodes.h"
#include "dsp56kBase/logging.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace
{
	constexpr dsp56k::TWord g_code = 0x100;
	constexpr dsp56k::TWord g_data = 0x40;	// r registers point around here

	struct Machine
	{
		dsp56k::DefaultMemoryValidator validator;
		dsp56k::Memory memory{validator, 0x1000, 0x1000, 0x800};
		dsp56k::PeripheralsNop x, y;
		dsp56k::DSP dsp{memory, &x, &y};

		explicit Machine(const uint32_t _block = 1)
		{
			auto config = dsp.getJit().getConfig();
			config.linkJitBlocks = false;
			config.dynamicFastInterrupts = true;
			config.maxInstructionsPerBlock = _block;
			config.maxDoIterations = 1;
			dsp.getJit().setConfig(config);
		}
	};

	struct State
	{
		uint64_t a, b, x, y;
		dsp56k::TWord sr, r[8], n[8], mem[2][0x100];
	};

	State randomState(std::mt19937_64& _rng, const dsp56k::TWord _srHigh)
	{
		State s{};
		auto acc = [&] { return (_rng() & 0x00ffffffffffffffull) << 8; };	// left-aligned, low 8 bits zero
		// Sign-extend the 56-bit value properly half the time, so "normal" values dominate.
		auto accNormal = [&] { const int64_t v = static_cast<int64_t>(_rng()) >> 16; return static_cast<uint64_t>(v) << 8; };
		s.a = (_rng() & 1) ? acc() : accNormal();
		s.b = (_rng() & 1) ? acc() : accNormal();
		s.x = _rng() & 0xffffffffffffull;
		s.y = _rng() & 0xffffffffffffull;
		s.sr = _srHigh | static_cast<dsp56k::TWord>(_rng() & 0xff);
		for(uint32_t i = 0; i < 8; ++i)
		{
			s.r[i] = g_data + static_cast<dsp56k::TWord>(_rng() % 0x40);
			s.n[i] = static_cast<dsp56k::TWord>(_rng() % 8);
		}
		for(auto& area : s.mem)
			for(auto& w : area)
				w = static_cast<dsp56k::TWord>(_rng() & 0xffffff);
		return s;
	}

	void load(Machine& _m, const State& _s, const std::vector<dsp56k::TWord>& _code)
	{
		auto& r = _m.dsp.regs();
		r.a.var = _s.a;
		r.b.var = _s.b;
		r.x.var = _s.x;
		r.y.var = _s.y;
		for(uint32_t i = 0; i < 8; ++i)
		{
			r.r[i].var = _s.r[i];
			r.n[i].var = _s.n[i];
		}
		for(dsp56k::TWord i = 0; i < 0x100; ++i)
		{
			_m.memory.set(dsp56k::MemArea_X, i, _s.mem[0][i]);
			_m.memory.set(dsp56k::MemArea_Y, i, _s.mem[1][i]);
		}
		for(dsp56k::TWord i = 0; i < 48; ++i)
			_m.dsp.memWriteP(g_code + i, i < _code.size() ? _code[i] : 0);
		r.sr.var = _s.sr;
		_m.dsp.getJit().checkModeChange();
		_m.dsp.setPC(g_code);
	}

	std::string disasm(const dsp56k::TWord _w0, const dsp56k::TWord _w1)
	{
		static dsp56k::Opcodes opcodes;
		static dsp56k::Disassembler dis(opcodes);
		std::string out;
		dis.disassemble(out, _w0, _w1, 0, 0, g_code);
		return out;
	}
}

int main(int argc, char** argv)
{
	Logging::setLogFunc([](const std::string&) {});
	const uint32_t trials = argc > 1 ? static_cast<uint32_t>(std::atoi(argv[1])) : 500;
	// Window mode: with K > 0, every run of K instructions is one JIT block on one machine and K
	// single-instruction blocks on the other, so what is compared is how the JIT carries state
	// (lazy CCR, cached registers) from one instruction to the next inside a block.
	const uint32_t window = argc > 2 ? static_cast<uint32_t>(std::atoi(argv[2])) : 0;

	std::vector<dsp56k::TWord> words;
	std::string line;
	while(std::getline(std::cin, line))
		if(!line.empty())
			words.push_back(static_cast<dsp56k::TWord>(std::stoul(line, nullptr, 16)));

	std::mt19937_64 rng(1234);
	uint32_t bad = 0;
	for(size_t i = 0; i < words.size(); ++i)
	{
		const auto w0 = words[i];
		const auto w1 = i + 1 < words.size() ? words[i + 1] : 0;
		const auto text = disasm(w0, w1);
		if(std::getenv("JITDIFF_TRACE"))
			std::fprintf(stderr, "%zu %06x %s\n", i, w0, text.c_str());
		// Program flow the harness cannot contain: loops, returns, stack and mode changes, jumps to
		// absolute addresses outside the test memory.
		static const char* const skip[] = {"dc ", "do ", "dor ", "rti", "rts", "jsr", "bsr", "enddo", "wait", "stop", "reset", "movec", "movem", "rep", "illegal", "jmp", "jcc", "jscc", "brk", "debug", "trap"};
		bool skipIt = false;
		const size_t span = window ? window : 1;
		std::string texts;
		for(size_t k = 0; k < span && !skipIt; ++k)
		{
			if(i + k >= words.size()) { skipIt = true; break; }
			const auto tk = k ? disasm(words[i + k], i + k + 1 < words.size() ? words[i + k + 1] : 0) : text;
			for(const auto* sk : skip)
				if(tk.find(sk) == 0)
					skipIt = true;
			texts += (k ? " | " : "") + tk.substr(0, tk.find_last_not_of(' ') + 1);
		}
		if(skipIt)
			continue;
		std::vector<dsp56k::TWord> code(words.begin() + static_cast<std::ptrdiff_t>(i), words.begin() + static_cast<std::ptrdiff_t>(std::min(words.size(), i + span + 1)));

		uint32_t mismatches = 0;
		std::string first;
		auto jit = std::make_unique<Machine>(window ? window : 1);
		auto itp = std::make_unique<Machine>(1);
		for(uint32_t t = 0; t < trials; ++t)
		{
			const dsp56k::TWord srHigh = (t & 1) ? 0xc10000 : 0xc00000;
			const auto s = randomState(rng, srHigh);
			load(*jit, s, code);
			load(*itp, s, code);
			jit->dsp.exec();
			if(window)
			{
				// Step the reference, one instruction per block, to where the block stopped.
				for(uint32_t k = 0; k < 3 * window && itp->dsp.getPC().toWord() != jit->dsp.getPC().toWord(); ++k)
					itp->dsp.exec();
			}
			else
				itp->dsp.execInterpreter();

			const auto& rj = jit->dsp.regs();
			const auto& ri = itp->dsp.regs();
			char buf[512];
			std::string diff;
			auto cmp = [&](const char* _name, uint64_t _j, uint64_t _i)
			{
				if(_j == _i) return;
				std::snprintf(buf, sizeof(buf), " %s jit=%llx interp=%llx", _name, (unsigned long long)_j, (unsigned long long)_i);
				diff += buf;
			};
			cmp("a", rj.a.var, ri.a.var);
			cmp("b", rj.b.var, ri.b.var);
			cmp("x", rj.x.var, ri.x.var);
			cmp("y", rj.y.var, ri.y.var);
			cmp("sr", jit->dsp.getSR().var, itp->dsp.getSR().var);
			cmp("pc", rj.pc.var, ri.pc.var);
			for(uint32_t k = 0; k < 8; ++k)
			{
				char n[8];
				std::snprintf(n, sizeof(n), "r%u", k);
				cmp(n, rj.r[k].var, ri.r[k].var);
			}
			for(dsp56k::TWord k = 0; k < 0x100; ++k)
			{
				cmp("x:mem", jit->memory.get(dsp56k::MemArea_X, k), itp->memory.get(dsp56k::MemArea_X, k));
				cmp("y:mem", jit->memory.get(dsp56k::MemArea_Y, k), itp->memory.get(dsp56k::MemArea_Y, k));
			}
			if(!diff.empty())
			{
				if(!mismatches)
				{
					std::snprintf(buf, sizeof(buf), "   start: a=%llx b=%llx x=%llx y=%llx sr=%06x\n   diff:",
						(unsigned long long)s.a, (unsigned long long)s.b, (unsigned long long)s.x, (unsigned long long)s.y, s.sr);
					first = buf + diff;
				}
				++mismatches;
			}
		}
		if(mismatches)
		{
			++bad;
			std::printf("%06x  %-40s  %u/%u trials differ\n%s\n", w0, (window ? texts : text).c_str(), mismatches, trials, first.c_str());
		}
	}
	std::printf("%u of %zu instructions disagree\n", bad, words.size());
	return bad ? 1 : 0;
}
