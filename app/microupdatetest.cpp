// microupdatetest <os image> <stream.syx>: the stream (g1update.py build, as NME sends it) goes through
// the Micro's update-mode stand-in in odd-sized pieces; the image it stores must be the one it came from.
#include "microupdate.h"
#include <cstdio>
#include <fstream>
#include <iterator>

static std::vector<uint8_t> load(const char* _p)
{
	std::ifstream f(_p, std::ios::binary);
	return {std::istreambuf_iterator<char>(f), {}};
}

int main(int argc, char** argv)
{
	if(argc != 3)
		return 77;
	const auto image = load(argv[1]), stream = load(argv[2]);
	if(image.empty() || stream.empty())
		return 77;
	g1app::MicroUpdate u;
	std::vector<uint8_t> reply;
	for(size_t i = 0; i < stream.size(); i += 97)
		u.feed({stream.begin() + i, stream.begin() + std::min(stream.size(), i + 97)}, reply);
	std::printf("%s\n", u.describe().c_str());
	if(u.state() != g1app::MicroUpdate::State::Ready || reply.size() < 18)
		return 1;
	u.press();
	std::vector<uint8_t> got;
	if(!u.storedImage(got) || got != image)
	{
		std::printf("stored image differs (%zu vs %zu bytes)\n", got.size(), image.size());
		return 1;
	}
	std::printf("ok: %zu bytes stored\n", got.size());
	return 0;
}
