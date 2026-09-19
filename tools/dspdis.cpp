// dspdis: disassembles DSP56300 words (one hex word per line, on stdin).
//   dspdis [start_PC_hex] < words.hex
#include "dsp56kEmu/disasm.h"
#include "dsp56kEmu/opcodes.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
	std::vector<uint32_t> mem;
	std::string line;
	while(std::getline(std::cin, line))
		if(!line.empty())
			mem.push_back(static_cast<uint32_t>(std::stoul(line, nullptr, 16)));
	const auto pc = argc > 1 ? static_cast<dsp56k::TWord>(std::stoul(argv[1], nullptr, 16)) : 0u;
	dsp56k::Opcodes opcodes;
	dsp56k::Disassembler dis(opcodes);
	std::vector<uint32_t> full(pc, 0);
	full.insert(full.end(), mem.begin(), mem.end());
	std::string out;
	dis.disassembleMemoryBlock(out, full, pc, false, true, true);
	std::cout << out;
}
