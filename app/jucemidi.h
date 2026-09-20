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
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace g1app
{
	class JuceMidi
	{
	public:
		explicit JuceMidi(const char* _clientName) : m_clientName(_clientName) {}

		bool valid() const { return true; }		// there is no session to open: ports stand alone
		int clientId() const { return -1; }		// an ALSA notion; nothing to report here

		// True when every port asked for was really created. False means the system would not
		// make virtual ports (Windows without MIDI Services) and only real devices can be used.
		bool virtualPorts() const { return m_virtual; }

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
				m_virtual = false;
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
			auto& out = m_ports[static_cast<size_t>(_index)].out;
			if(!out)
				return;
			// The G1 speaks in whole messages, SysEx included; JUCE wants them one at a time.
			size_t i = 0;
			while(i < _bytes.size())
			{
				const auto used = messageLength(_bytes, i);
				if(used == 0)
					break;
				out->sendMessageNow(juce::MidiMessage(_bytes.data() + i, static_cast<int>(used)));
				i += used;
			}
		}

	private:
		// How many bytes the message starting at _at occupies. Running status does not appear on
		// the G1's ports: the OS always sends a status byte.
		static size_t messageLength(const std::vector<uint8_t>& _b, const size_t _at)
		{
			const auto s = _b[_at];
			if(s == 0xf0)
			{
				for(size_t i = _at + 1; i < _b.size(); ++i)
					if(_b[i] == 0xf7)
						return i - _at + 1;
				return _b.size() - _at;		// unterminated: send what there is
			}
			if(s >= 0xf8)
				return 1;
			switch(s & 0xf0)
			{
			case 0xc0: case 0xd0:	return std::min<size_t>(2, _b.size() - _at);
			case 0xf0:				return s == 0xf1 || s == 0xf3 ? std::min<size_t>(2, _b.size() - _at)
													  : (s == 0xf2 ? std::min<size_t>(3, _b.size() - _at) : 1);
			default:				return std::min<size_t>(3, _b.size() - _at);
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
		};

		std::string m_clientName;
		std::vector<Port> m_ports;
		Collector m_collector;
		bool m_virtual = true;
	};
}
