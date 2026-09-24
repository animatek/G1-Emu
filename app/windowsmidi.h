#pragma once

#include "miditransport.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace g1app
{
	// Own bidirectional endpoints, scoped to this process's Windows MIDI Services session.
	class WindowsMidi final : public MidiTransport
	{
	public:
		explicit WindowsMidi(const char* clientName);
		~WindowsMidi() override;
		bool valid() const;
		bool virtualPorts() const;
		const std::string& error() const;
		// The ports are always our own: a manual device choice has no meaning here.
		int addPort(const char* name, const PortDevices& = {}) override;
		void poll(std::vector<std::vector<uint8_t>>& perPort) override;
		void send(int index, const std::vector<uint8_t>& bytes) override;
		std::string describe() const override;
	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
