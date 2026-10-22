#include "audiobridge.h"

#include <cstdio>
#include <cstdlib>
#include <limits>
#include <thread>

namespace
{
	void check(const bool _ok, const char* _message)
	{
		if(!_ok)
		{
			std::fprintf(stderr, "FAIL: %s\n", _message);
			std::exit(1);
		}
	}
}

int main()
{
	// Full, empty, partial and wrapped discards must preserve the remaining FIFO order.
	g1app::FrameRing<1> ring(4);
	check(ring.skip(10) == 0, "skip empty");
	for(int i = 0; i < 4; ++i)
		check(ring.push({static_cast<float>(i)}), "fill ring");
	check(!ring.push({99}), "full ring");
	check(ring.skip(3) == 3, "partial skip");
	for(int i = 4; i < 7; ++i)
		check(ring.push({static_cast<float>(i)}), "wrap producer");
	std::array<float, 1> frame;
	check(ring.pop(frame) && frame[0] == 3, "oldest retained frame");
	check(ring.skip(2) == 2, "wrapped skip");
	check(ring.pop(frame) && frame[0] == 6, "last retained frame");
	check(ring.push({7}) && ring.skip(100) == 1, "skip limited to available");

	// Exercise the SPSC ownership during concurrent producer writes and batch releases.
	g1app::FrameRing<1> concurrent(64);
	constexpr size_t count = 100000;
	std::thread producer([&] {
		for(size_t i = 0; i < count; ++i)
			while(!concurrent.push({static_cast<float>(i)})) std::this_thread::yield();
	});
	for(size_t next = 0; next < count;)
	{
		if(concurrent.pop(frame))
		{
			check(frame[0] == static_cast<float>(next), "concurrent FIFO order");
			++next;
		}
		next += concurrent.skip(7);
	}
	producer.join();

	// Both input entry points must reject non-finite samples and bound finite extremes.
	for(const bool render : {false, true})
	{
		g1app::AudioBridge bridge(1);
		bridge.setRate(96000);
		const float values[] = {std::numeric_limits<float>::quiet_NaN(),
			std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
			std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), 0.5f};
		const float expected[] = {0, 0, 0, 1, -1, 0.5f};
		const float* inputs[] = {values};
		if(render)
			bridge.render(nullptr, 0, inputs, 1, 6);
		else
			bridge.pushInputs(inputs, 1, 0, 6);
		int32_t l = 0, r = 0;
		bridge.pullInput(l, r); // interpolation starts with the previous (silent) sample
		check(l == 0 && r == 0, "initial input silence");
		for(const auto value : expected)
		{
			bridge.pullInput(l, r);
			check(l == static_cast<int32_t>(value * 8388607.0f) && r == 0, "sanitized input");
		}
	}

	// Device restart rate writes may overlap output conversion and DSP input reads.
	g1app::AudioBridge bridge(1);
	std::thread restart([&] {
		for(int i = 0; i < 100000; ++i) bridge.setRate(i % 2 ? 44100 : 96000);
	});
	for(int i = 0; i < 100000; ++i)
	{
		bridge.push(1, 2, 3, 4);
		int32_t l, r;
		bridge.pullInput(l, r);
		bridge.skip(2);
	}
	restart.join();
	bridge.setRate(0);
	check(bridge.rate() == 48000, "default rate");
	std::puts("AudioBridge tests passed");
}
