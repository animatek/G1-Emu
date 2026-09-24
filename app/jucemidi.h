#pragma once

// MIDI through JUCE, the same two ports the hardware has: "PC Port" for the editor and "MIDI" for
// notes and controllers. They are **virtual** ports, created by the program and not by any cable,
// so an editor or a DAW on the same machine sees a G1 that is not there.
//
// This is not uniform across systems and it is worth knowing which is which:
//   macOS   CoreMIDI creates them natively, with nothing to install. The best of the three.
//   Linux   the ALSA sequencer does. The native backend (alsamidi.h) uses it directly.
//   Windows only through Windows MIDI Services; with the older WinRT or WinMM backends JUCE
//           cannot create one and createNewDevice returns nothing. When that happens this class
//           still works for talking to real MIDI hardware, says so through virtualPorts(), and
//           it is up to the caller to tell the user (see ROADMAP.md, point 5).
//
// addPort() can also be told to open an existing system MIDI device by name instead of creating
// a new one, one name for the output half and one for the input half. This is the manual patch
// used on Windows while Windows MIDI Services cannot own ports (Microsoft/MIDI issue #1047,
// docs/windows-build.md): the user runs a loopback driver such as loopMIDI, names two ports "G1
// PC Port" and "G1 MIDI" (or reuses whatever names loopMIDI gave them), points G1-Emu's Settings
// at them, and points the editor at the very same names. Every other caller leaves the names
// empty and behaviour is exactly as before.
//
// It implements MidiTransport, like AlsaMidi, so EmuHost does not care which one it has.

#include "miditransport.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace g1app
{
	class JuceMidi final : public MidiTransport
	{
	public:
		explicit JuceMidi(const char* _clientName) : m_clientName(_clientName) {}

		// True when every port asked for was really created or, for a manually-paired one, really
		// opened. False means something was neither: on Windows without MIDI Services and with no
		// manual device chosen, or a chosen device that no longer exists.
		bool virtualPorts() const { return m_virtual; }

		// Creates a port and returns its index. Input and output share a name, like a DIN pair --
		// unless _manual names an existing system MIDI device to open instead of creating a new
		// one (see the class comment). Either direction can be set without the other.
		int addPort(const char* _name, const PortDevices& _manual = {}) override
		{
			auto& port = m_ports.emplace_back();
			port.shortName = _name;
			port.manual = _manual;
			port.name = juce::String(m_clientName) + " " + _name;

			port.out = _manual.out.empty() ? juce::MidiOutput::createNewDevice(port.name)
										   : openExistingOutput(_manual.out);
			port.in = _manual.in.empty() ? juce::MidiInput::createNewDevice(port.name, &m_collector)
										 : openExistingInput(_manual.in);
			if(!port.out || !port.in)
				m_virtual = false;
			if(port.in)
			{
				m_collector.add(static_cast<int>(m_ports.size()) - 1, port.in.get());
				port.in->start();
			}
			return static_cast<int>(m_ports.size()) - 1;
		}

		// The system's MIDI devices, for a Settings view to list as manual-pairing choices.
		static std::vector<std::string> availableOutputs()
		{
			std::vector<std::string> names;
			for(const auto& d : juce::MidiOutput::getAvailableDevices())
				names.push_back(d.name.toStdString());
			return names;
		}
		static std::vector<std::string> availableInputs()
		{
			std::vector<std::string> names;
			for(const auto& d : juce::MidiInput::getAvailableDevices())
				names.push_back(d.name.toStdString());
			return names;
		}

		// Where the ports are, or why they are not (which depends on the system: this is the
		// class that runs on all three).
		std::string describe() const override
		{
			bool manual = false;
			std::string detail;
			const auto shown = [](const std::string& _s) { return _s.empty() ? std::string("auto") : _s; };
			for(const auto& port : m_ports)
			{
				manual = manual || !port.manual.out.empty() || !port.manual.in.empty();
				detail += (detail.empty() ? "" : ", ") + port.shortName + " out=" + shown(port.manual.out)
					+ " in=" + shown(port.manual.in);
			}
			if(m_virtual)
				return manual ? "manual MIDI devices (patch until Windows MIDI Services can own ports): " + detail
							  : "G1-Emu PC Port (editor) and G1-Emu MIDI";
			if(manual)
				// The user pointed Settings at real device names, and at least one of them could not
				// be opened -- most likely the loopback driver was not running, or the names changed.
				return "could not open one of the chosen MIDI devices: " + detail
					+ "; check Settings and that the loopback driver is running";
#if defined(_WIN32)
			return "Windows did not create G1-Emu's owned MIDI ports; this Windows MIDI "
				"Services version cannot expose the emulated instrument yet, pick manual MIDI "
				"devices in Settings as a patch until it does";
#else
			return "no virtual MIDI ports on this system: no editor can reach the G1";
#endif
		}

		// Collects everything that has arrived, split by port.
		void poll(std::vector<std::vector<uint8_t>>& _perPort) override
		{
			_perPort.resize(m_ports.size());
			m_collector.take(_perPort);
		}

		// Sends raw bytes through a port.
		void send(const int _index, const std::vector<uint8_t>& _bytes) override
		{
			if(_bytes.empty() || _index < 0 || static_cast<size_t>(_index) >= m_ports.size())
				return;
			auto& port = m_ports[static_cast<size_t>(_index)];
			if(!port.out)
				return;
			// A message can arrive split across two calls: the run loop drains the DUART's
			// transmit buffer every 2 ms of emulated time (EmuHost::run), and the PC Port's
			// SysEx replies (12 bytes for the NME handshake, far more for a patch) can take the
			// OS longer than that to finish writing -- more so under real-machine CPU load, not
			// something a Linux dev box ever sees running faster than real time. What was left
			// unfinished last time is picked up first, so a message is never sent until whole.
			auto& pending = port.pending;
			pending.insert(pending.end(), _bytes.begin(), _bytes.end());
			// The G1 speaks in whole messages, SysEx included; JUCE wants them one at a time.
			size_t i = 0;
			while(i < pending.size())
			{
				const auto used = messageLength(pending, i);
				if(used == 0)
					break;		// incomplete: the rest is due on a later call
				port.out->sendMessageNow(juce::MidiMessage(pending.data() + i, static_cast<int>(used)));
				i += used;
			}
			pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(i));
			// A message that never completes (corrupt stream, not a real G1) would otherwise
			// block this port forever behind it and grow without bound; the whole flash is 1 MB,
			// so no real SysEx from the G1 ever approaches that.
			if(pending.size() > 1024 * 1024)
				pending.clear();
		}

	private:
		// Opens an existing system MIDI device by name for the manual-pairing patch. Matches by
		// name rather than by identifier: identifiers are not guaranteed stable across restarts or
		// reboots, while a loopback driver's port names are what the user actually picked and typed
		// into the editor too.
		static std::unique_ptr<juce::MidiOutput> openExistingOutput(const std::string& _name)
		{
			for(const auto& d : juce::MidiOutput::getAvailableDevices())
				if(d.name.toStdString() == _name)
					return juce::MidiOutput::openDevice(d.identifier);
			return nullptr;
		}
		std::unique_ptr<juce::MidiInput> openExistingInput(const std::string& _name)
		{
			for(const auto& d : juce::MidiInput::getAvailableDevices())
				if(d.name.toStdString() == _name)
					return juce::MidiInput::openDevice(d.identifier, &m_collector);
			return nullptr;
		}

		// How many bytes the message starting at _at occupies, or 0 if _b does not yet hold all
		// of it -- the caller then waits for the rest instead of sending a truncated message.
		// Running status does not appear on the G1's ports: the OS always sends a status byte.
		static size_t messageLength(const std::vector<uint8_t>& _b, const size_t _at)
		{
			const auto s = _b[_at];
			const auto avail = _b.size() - _at;
			if(s == 0xf0)
			{
				for(size_t i = _at + 1; i < _b.size(); ++i)
					if(_b[i] == 0xf7)
						return i - _at + 1;
				return 0;		// no terminator yet
			}
			if(s >= 0xf8)
				return 1;
			switch(s & 0xf0)
			{
			case 0xc0: case 0xd0:	return avail >= 2 ? 2 : 0;
			case 0xf0:				return s == 0xf1 || s == 0xf3 ? (avail >= 2 ? 2 : 0)
													  : (s == 0xf2 ? (avail >= 3 ? 3 : 0) : 1);
			default:				return avail >= 3 ? 3 : 0;
			}
		}

		// One callback for every input, keeping the bytes of each port apart.
		class Collector final : public juce::MidiInputCallback
		{
		public:
			// Keyed by the device itself and not by its identifier: JUCE gives every virtual
			// port the same identifier on Linux, so a map keyed by that puts every message in
			// whichever port was created last.
			void add(const int _index, const juce::MidiInput* _device)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_byDevice[_device] = _index;
			}

			void handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _message) override
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				const auto it = m_byDevice.find(_source);
				if(it == m_byDevice.end())
					return;
				auto& dst = m_bytes[it->second];
				const auto* raw = _message.getRawData();
				dst.insert(dst.end(), raw, raw + _message.getRawDataSize());
			}

			// JUCE reassembles a SysEx that arrives in several packets (CoreMIDI, ALSA) and only
			// calls handleIncomingMidiMessage once it has the F7. It calls this instead when the
			// stream ended without one, so what it carries is a truncated message that JUCE
			// discards right after. Nothing to rebuild from it, but if a patch upload dies on a
			// Mac this line is the tell: with G1_MIDI_LOG=1 it says a SysEx was cut short.
			void handlePartialSysexMessage(juce::MidiInput*, const juce::uint8* _data, int _size, double) override
			{
				static const bool log = [] { const char* v = std::getenv("G1_MIDI_LOG"); return v && *v && *v != '0'; }();
				if(!log)
					return;
				std::printf("[midi] in  truncated SysEx, %d bytes dropped by JUCE (no F7): %02x %02x ...\n",
					_size, _size > 0 ? _data[0] : 0, _size > 1 ? _data[1] : 0);
				std::fflush(stdout);
			}

			void take(std::vector<std::vector<uint8_t>>& _perPort)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				for(auto& [index, bytes] : m_bytes)
				{
					if(index < 0 || static_cast<size_t>(index) >= _perPort.size() || bytes.empty())
						continue;
					auto& dst = _perPort[static_cast<size_t>(index)];
					dst.insert(dst.end(), bytes.begin(), bytes.end());
					bytes.clear();
				}
			}

		private:
			std::mutex m_mutex;
			std::map<const juce::MidiInput*, int> m_byDevice;
			std::map<int, std::vector<uint8_t>> m_bytes;
		};

		struct Port
		{
			juce::String name;
			std::string shortName;			// "PC Port" or "MIDI", as EmuHost named it
			PortDevices manual;				// the devices chosen by hand for it, if any
			std::unique_ptr<juce::MidiOutput> out;
			std::unique_ptr<juce::MidiInput> in;
			std::vector<uint8_t> pending;	// a message send() has not seen the end of yet
		};

		std::string m_clientName;
		Collector m_collector;
		std::vector<Port> m_ports; // Stop input callbacks before destroying their collector.
		bool m_virtual = true;
	};
}
