#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace g1app
{
	// Own bidirectional endpoints, scoped to this process's Windows MIDI Services session.
	class WindowsMidi
	{
	public:
		explicit WindowsMidi(const char* clientName);
		~WindowsMidi();
		bool valid() const;
		bool virtualPorts() const;
		const std::string& error() const;
		int addPort(const char* name);
		void poll(std::vector<std::vector<uint8_t>>& perPort);
		void send(int index, const std::vector<uint8_t>& bytes);
	private:
		struct Impl;
		std::unique_ptr<Impl> m_impl;
	};
}
