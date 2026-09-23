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
// Interface deliberately the same as AlsaMidi's, so EmuHost does not care which one it has.

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace g1app
{
	class JuceMidi
	{
	public:
		// _preferred names the MIDI cables to bind to, in PC Port, MIDI order ("G1,loopMIDI
		// Port"), when virtual ports cannot be made and the fallback runs. Empty: the first
		// free cables are taken. From the settings file; G1_MIDI_DEVICES wins over it.
		explicit JuceMidi(const char* _clientName, const std::string& _preferred = {})
		: m_clientName(_clientName), m_preferred(_preferred)
		{
		}

		bool valid() const { return true; }		// there is no session to open: ports stand alone
		int clientId() const { return -1; }		// an ALSA notion; nothing to report here

		// True when every port asked for was really created. False means the system would not
		// make virtual ports (Windows without MIDI Services) and only real devices can be used.
		bool virtualPorts() const { return m_virtual; }

		// Which real device each port ended up on, for the status line. Empty when the ports
		// are virtual (they are their own devices, there is nothing to report) or when no
		// device could be opened at all.
		std::vector<std::string> devices() const
		{
			std::vector<std::string> result;
			if(m_virtual)
				return result;
			for(const auto& port : m_ports)
				result.push_back(port.dead ? std::string() : port.device);
			return result;
		}

		// Every MIDI cable the system has (a device with the same name on the input and the
		// output side), for the settings window to offer as a choice. Virtual ports made by
		// this program are their own devices and never appear here.
		static std::vector<std::string> cables()
		{
			std::vector<std::string> result;
			std::set<std::string> outputs;
			for(const auto& d : juce::MidiOutput::getAvailableDevices())
				outputs.insert(d.name.toStdString());
			for(const auto& d : juce::MidiInput::getAvailableDevices())
			{
				const auto name = d.name.toStdString();
				if(outputs.count(name) && std::find(result.begin(), result.end(), name) == result.end())
					result.push_back(name);
			}
			return result;
		}

		// A human-readable one-liner for the status line: "PC Port -> loopMIDI Port 1" or,
		// when no real device was there either, "PC Port (dead)".
		std::string describe() const
		{
			std::string result;
			for(size_t i = 0; i < m_ports.size(); ++i)
			{
				if(i)
					result += ", ";
				result += m_ports[i].name.toStdString() + (m_ports[i].dead
					? " (dead: no device)" : " -> " + m_ports[i].device);
			}
			return result;
		}

		// Creates a port and returns its index. Input and output share a name, like a DIN pair.
		int addPort(const char* _name)
		{
			auto& port = m_ports.emplace_back();
			port.name = juce::String(m_clientName) + " " + _name;

			port.out = juce::MidiOutput::createNewDevice(port.name);
			port.in = juce::MidiInput::createNewDevice(port.name, &m_collector);
			if(port.in)
			{
				m_collector.add(static_cast<int>(m_ports.size()) - 1, port.in.get());
				port.in->start();
			}
			if(!port.out || !port.in)
			{
				m_virtual = false;
				// The system would not make virtual ports (Windows needs its new MIDI
				// Services for that, which JUCE only reaches through JUCE_USE_WINDOWS_MIDI_SERVICES).
				// Without a cable nothing reaches the G1, so the pair falls back to real
				// devices. A MIDI cable is a device with the same name on the input and the
				// output side (loopMIDI's cables are exactly that; the GS Wavetable Synth has
				// no input and a sound card's ports have different names, so neither is one),
				// and both directions of a port must be the SAME cable: the editor's requests
				// come in on one end and the G1's replies go out the other, through the very
				// same cable, or no handshake can ever complete. A cable does not echo back to
				// the process that writes to it (measured), so the pair is safe to open both
				// ways. G1_MIDI_DEVICES, when set, names the cables in PC Port, MIDI order
				// ("G1,loopMIDI Port"); otherwise the first two free cables are taken.
				port.device = pickCable();
				if(!port.device.empty())
				{
					port.out = openCableOutput(port.device);
					port.in = openCableInput(port.device);
				}
				if(port.in)
				{
					// Same registration the virtual port gets: without it the collector
					// drops every message this device delivers.
					m_collector.add(static_cast<int>(m_ports.size()) - 1, port.in.get());
					port.in->start();
				}
				if(!port.out || !port.in)
					port.dead = true;
			}
			return static_cast<int>(m_ports.size()) - 1;
		}

		// Collects everything that has arrived, split by port.
		void poll(std::vector<std::vector<uint8_t>>& _perPort)
		{
			_perPort.resize(m_ports.size());
			m_collector.take(_perPort);
		}

		// Sends raw bytes through a port.
		void send(const int _index, const std::vector<uint8_t>& _bytes)
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
		// A MIDI cable, for the fallback: a device with the same name on the input and the
		// output side. G1_MIDI_DEVICES, when set, names them in PC Port, MIDI order
		// ("G1,loopMIDI Port"); otherwise the first cable not already taken by another port
		// is used. The empty string when the system has none.
		std::string pickCable()
		{
			std::set<std::string> outputs;
			for(const auto& d : juce::MidiOutput::getAvailableDevices())
				outputs.insert(d.name.toStdString());

			// What the settings window asked for wins over taking the first free cable.
			// m_preferred comes from the settings file, and EmuHost::start() has already
			// let G1_MIDI_DEVICES win over that, the same way as every other setting.
			std::vector<std::string> wanted = splitList(m_preferred);
			const auto port = static_cast<int>(m_ports.size());
			if(static_cast<size_t>(port) < wanted.size() && !wanted[static_cast<size_t>(port)].empty()
				&& outputs.count(wanted[static_cast<size_t>(port)]))
				return wanted[static_cast<size_t>(port)];

			for(const auto& d : juce::MidiInput::getAvailableDevices())
			{
				const auto name = d.name.toStdString();
				if(!outputs.count(name) || m_usedDevices.count(name))
					continue;
				return name;
			}
			return {};
		}

		// "G1, loopMIDI Port" -> {"G1", "loopMIDI Port"}; empty names are kept (a
		// comma with nothing in it means "take the first free one for that port").
		static std::vector<std::string> splitList(const std::string& _list)
		{
			std::vector<std::string> result;
			size_t at = 0;
			while(at <= _list.size())
			{
				const auto comma = _list.find(',', at);
				auto name = _list.substr(at, comma == std::string::npos ? std::string::npos : comma - at);
				const auto a = name.find_first_not_of(" 	");
				const auto b = name.find_last_not_of(" 	");
				result.push_back(a == std::string::npos ? std::string() : name.substr(a, b - a + 1));
				if(comma == std::string::npos)
					break;
				at = comma + 1;
			}
			return result;
		}

		std::unique_ptr<juce::MidiOutput> openCableOutput(const std::string& _cable)
		{
			for(const auto& d : juce::MidiOutput::getAvailableDevices())
			{
				if(d.name.toStdString() != _cable)
					continue;
				auto out = juce::MidiOutput::openDevice(d.identifier);
				if(out)
				{
					m_usedDevices.insert(_cable);
					return out;
				}
			}
			return nullptr;
		}

		std::unique_ptr<juce::MidiInput> openCableInput(const std::string& _cable)
		{
			for(const auto& d : juce::MidiInput::getAvailableDevices())
			{
				if(d.name.toStdString() != _cable)
					continue;
				auto in = juce::MidiInput::openDevice(d.identifier, &m_collector);
				if(in)
				{
					m_usedDevices.insert(_cable);
					return in;
				}
			}
			return nullptr;
		}

		// How many bytes the message starting at _at occupies, or 0 if the buffer does not yet
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

			void handlePartialSysexMessage(juce::MidiInput*, const juce::uint8*, int, double) override {}

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
			std::unique_ptr<juce::MidiOutput> out;
			std::unique_ptr<juce::MidiInput> in;
			std::vector<uint8_t> pending;	// a message send() has not seen the end of yet
			std::string device;			// the real device this port fell back to (virtual: empty)
			bool dead = false;			// neither virtual nor real: nothing to talk to
		};

		std::string m_clientName;
		std::string m_preferred;
		std::vector<Port> m_ports;
		Collector m_collector;
		std::set<std::string> m_usedDevices;
		bool m_virtual = true;
	};
}
