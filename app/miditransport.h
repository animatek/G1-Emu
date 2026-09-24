#pragma once

// MidiTransport: how the bytes of the G1's two MIDI ports get in and out of the emulator. The
// emulator only ever sees raw bytes per port, so every way of carrying them is one class behind
// this interface, and EmuHost knows none of them by name:
//
//   AlsaMidi     alsamidi.h      Linux, ALSA sequencer ports (the native backend)
//   JuceMidi     jucemidi.h      macOS CoreMIDI, Windows and Linux through JUCE
//   WindowsMidi  windowsmidi.h   Windows MIDI Services, opt-in and experimental
//
// The one place that chooses between them is makeMidiTransport() (miditransport.cpp). A new way to
// reach the emulator, such as a local socket straight from the editor, is a new class here and one
// line in that function.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace g1app
{
	// The system MIDI devices the user chose by hand to stand in for one port's two directions,
	// by name. Empty means "make the port ourselves". Only the JUCE backend has any use for it:
	// it is the patch for Windows while Windows MIDI Services cannot own ports (jucemidi.h).
	struct PortDevices
	{
		std::string out, in;
	};

	class MidiTransport
	{
	public:
		virtual ~MidiTransport() = default;

		// Creates a port and returns its index. _manual is ignored by the transports that always
		// own their ports.
		virtual int addPort(const char* _name, const PortDevices& _manual = {}) = 0;

		// Collects everything that has arrived, split by port index.
		virtual void poll(std::vector<std::vector<uint8_t>>& _perPort) = 0;

		// Sends raw bytes through a port.
		virtual void send(int _index, const std::vector<uint8_t>& _bytes) = 0;

		// Where the ports are, or why they are not, in one line for the status bar. Call it
		// after the ports have been added.
		virtual std::string describe() const = 0;

		// Only the ALSA transport has anything to link: programs that read raw MIDI devices and
		// not sequencer ports (Bitwig on Linux) see none of its ports, so it subscribes them to
		// the ports of a sound card the user loaded for that (docs/bitwig-midi.md). _card is the
		// card's ID. True once linked; _log takes what to tell the user, _summary the short form
		// for the status bar. Every other transport has nothing to do and says so with false.
		virtual bool linkRawCard(const std::string& _card, int _pcPort, int _midiPort,
			std::string& _log, std::string& _summary)
		{
			(void)_card; (void)_pcPort; (void)_midiPort; (void)_log; (void)_summary;
			return false;
		}
	};

	// The transport this build uses. Null when there is none at all (the ALSA sequencer would not
	// open), with the reason in _log; the emulator cannot run without one. A transport that opened
	// but cannot make its ports still comes back, and says so in describe().
	std::unique_ptr<MidiTransport> makeMidiTransport(const char* _clientName, std::string& _log);
}
