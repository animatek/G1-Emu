// Live Windows MIDI Services test. Creates only its own temporary virtual ports.
#include "windowsmidi.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <future>
#include <objbase.h>

struct Receiver : juce::MidiInputCallback
{
	std::mutex mutex;
	std::vector<uint8_t> bytes;
	void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override
	{
		std::lock_guard<std::mutex> lock(mutex);
		bytes.insert(bytes.end(), msg.getRawData(), msg.getRawData() + msg.getRawDataSize());
	}
	std::vector<uint8_t> take()
	{
		std::lock_guard<std::mutex> lock(mutex);
		return std::exchange(bytes, {});
	}
};

int checkPorts()
{
	const auto comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if(FAILED(comResult)) return 5;
	struct Apartment { ~Apartment() { CoUninitialize(); } } apartment;
	std::puts("Creating native MIDI session...");
	g1app::WindowsMidi ports("G1-Emu Test");
	std::puts("Creating PC port...");
	ports.addPort("PC");
	std::puts("Creating MIDI port...");
	ports.addPort("MIDI");
	if(!ports.virtualPorts())
	{
		std::fprintf(stderr, "Native ports failed: %s\n", ports.error().c_str());
		return 1;
	}
	Receiver receivers[2];
	std::unique_ptr<juce::MidiInput> inputs[2];
	std::unique_ptr<juce::MidiOutput> outputs[2];
	const juce::String names[]{"G1-Emu Test PC", "G1-Emu Test MIDI"};
	std::puts("Waiting for WinMM ports...");
	for(int attempt = 0; attempt < 300; ++attempt)
	{
		for(int i = 0; i < 2; ++i)
		{
			if(!inputs[i])
				for(const auto& d : juce::MidiInput::getAvailableDevices())
					if(d.name.startsWith(names[i])) inputs[i] = juce::MidiInput::openDevice(d.identifier, &receivers[i]);
			if(!outputs[i])
				for(const auto& d : juce::MidiOutput::getAvailableDevices())
					if(d.name.startsWith(names[i])) outputs[i] = juce::MidiOutput::openDevice(d.identifier);
		}
		if(inputs[0] && inputs[1] && outputs[0] && outputs[1]) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
	}
	for(int i = 0; i < 2; ++i)
	{
		if(!inputs[i] || !outputs[i])
		{
			std::fprintf(stderr, "Port %d not visible/openable through WinMM\n", i);
			for(const auto& d : juce::MidiInput::getAvailableDevices()) std::printf("Input: %s\n", d.name.toRawUTF8());
			for(const auto& d : juce::MidiOutput::getAvailableDevices()) std::printf("Output: %s\n", d.name.toRawUTF8());
			return 2;
		}
		inputs[i]->start();
	}
	const std::vector<uint8_t> messages[]{{0xf0, 0x7d, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0xf7}, {0x90, 60, 100}};
	for(int i = 0; i < 2; ++i)
		outputs[i]->sendMessageNow(juce::MidiMessage(messages[i].data(), static_cast<int>(messages[i].size())));
	std::vector<std::vector<uint8_t>> received;
	for(int attempt = 0; attempt < 100; ++attempt)
	{
		ports.poll(received);
		if(received[0] == messages[0] && received[1] == messages[1]) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	if(received[0] != messages[0] || received[1] != messages[1])
	{
		std::fprintf(stderr, "WinMM -> native failed: %zu/%zu bytes\n", received[0].size(), received[1].size());
		return 3;
	}
	ports.send(0, {0xf0, 0x7d, 1, 2, 3});
	ports.send(0, {4, 5, 6, 7, 8, 9, 0xf7});
	ports.send(1, messages[1]);
	std::vector<uint8_t> replies[2];
	for(int attempt = 0; attempt < 100; ++attempt)
	{
		for(int i = 0; i < 2; ++i)
		{
			const auto chunk = receivers[i].take();
			replies[i].insert(replies[i].end(), chunk.begin(), chunk.end());
		}
		if(replies[0] == messages[0] && replies[1] == messages[1]) break;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}
	if(replies[0] != messages[0] || replies[1] != messages[1])
	{
		std::fprintf(stderr, "Native -> WinMM failed: %zu/%zu bytes\n", replies[0].size(), replies[1].size());
		return 4;
	}
	std::puts("PASS: both native ports visible in WinMM; notes and fragmented SysEx round-trip without crossing ports.");
	return 0;
}

int main()
{
	std::setvbuf(stdout, nullptr, _IONBF, 0);
	juce::ScopedJuceInitialiser_GUI init;
	return std::async(std::launch::async, checkPorts).get();
}
