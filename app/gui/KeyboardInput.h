#pragma once

// The standalone's MIDI keyboard (#84): one of the system's MIDI inputs, played straight into the
// G1's MIDI IN beside the MIDI port, so a keyboard plays G1-Emu with no DAW and no patching of
// ports outside it. Picked in Settings and opened at once, no restart; HostOptions::keyboard keeps
// the name for the next start. Every system: on Windows the standalone cannot make a MIDI port of
// its own, on macOS there is nothing to connect a keyboard to an app's port with, and on Linux it
// saves aconnect or a patchbay.
//
// One for the whole window (get()), opened by the app once the G1 runs and closed before it stops:
// its callback, on JUCE's MIDI thread, hands the bytes to EmuHost::queueMidiIn under m_mutex.

#include "emuhost.h"
#include "hostlog.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace g1gui
{
	class KeyboardInput final : private juce::MidiInputCallback
	{
	public:
		static KeyboardInput& get()
		{
			static KeyboardInput k;
			return k;
		}

		// The system's MIDI inputs a keyboard could be, by name: not G1-Emu's own ports (this one's
		// would feed its MIDI OUT back into its MIDI IN) nor another G1-Emu's.
		static std::vector<std::string> devices(const g1app::EmuHost& _host)
		{
			const auto own = _host.ownMidiPortIds();
			std::vector<std::string> names;
			for(const auto& d : juce::MidiInput::getAvailableDevices())
			{
				if(d.name.contains("G1-Emu") || std::find(own.begin(), own.end(), d.identifier.toStdString()) != own.end())
					continue;
				names.push_back(d.name.toStdString());
			}
			return names;
		}

		// Opens the input named _name (empty: none) and closes the one before. False when the name
		// is not among the system's inputs right now or the system would not open it (on Windows a
		// device another program holds), with the reason in problem().
		bool open(g1app::EmuHost& _host, const std::string& _name)
		{
			close();
			std::lock_guard<std::mutex> lock(m_mutex);
			m_host = &_host;
			m_name = _name;
			m_problem.clear();
			if(_name.empty())
				return true;
			const auto own = _host.ownMidiPortIds();
			for(const auto& d : juce::MidiInput::getAvailableDevices())
			{
				if(d.name.toStdString() != _name || std::find(own.begin(), own.end(), d.identifier.toStdString()) != own.end())
					continue;
				m_in = juce::MidiInput::openDevice(d.identifier, this);
				if(!m_in)
				{
					m_problem = "the system would not open it (another program may be using it)";
					return false;
				}
				m_in->start();
				g1app::logEvent("keyboard: " + _name);
				return true;
			}
			m_problem = "not connected";
			return false;
		}

		void close()
		{
			std::unique_ptr<juce::MidiInput> in;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				in = std::move(m_in);
				m_host = nullptr;
			}
			if(in)
				in->stop();		// outside the lock: a callback running now may be waiting for it
		}

		const std::string& name() const { return m_name; }
		const std::string& problem() const { return m_problem; }

	private:
		KeyboardInput() = default;

		void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& _message) override
		{
			// A keyboard's SysEx is not for the G1, and its active sensing would only count as traffic.
			if(_message.isSysEx() || _message.isActiveSense())
				return;
			std::lock_guard<std::mutex> lock(m_mutex);
			if(m_host)
				m_host->queueMidiIn(_message.getRawData(), static_cast<size_t>(_message.getRawDataSize()));
		}

		std::mutex m_mutex;
		std::unique_ptr<juce::MidiInput> m_in;
		g1app::EmuHost* m_host = nullptr;
		std::string m_name, m_problem;
	};
}
