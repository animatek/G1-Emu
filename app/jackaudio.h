#pragma once

// Audio through JACK (with PipeWire, pipewire-jack): the emulated G1 shows up as the client "G1-Emu"
// with the connectors of the hardware's back panel: out_1..out_4 (mono outputs) and in_L/in_R.
// The G1's headphones are a copy of 1/2, so out_1/out_2 connect themselves to the first two
// physical outputs (G1_JACK_CONNECT=0 prevents it); everything else is routed by hand.
//
// This is the native Linux path and it stays, because the graph and these port names are worth
// having. The rate conversion and the queues between threads are AudioBridge's (audiobridge.h),
// shared with the JUCE backend that macOS and Windows use, so both sound alike by construction.

#include "audiobridge.h"

#include <jack/jack.h>

#include <array>
#include <cstdint>
#include <string>

namespace g1app
{
	class JackAudio
	{
	public:
		static constexpr double EmuRate = AudioBridge::EmuRate;

		JackAudio(const char* _name, const float _gain, const bool _autoConnect) : m_bridge(_gain)
		{
			m_client = jack_client_open(_name, JackNoStartServer, nullptr);
			if(!m_client)
				return;
			m_bridge.setRate(jack_get_sample_rate(m_client));
			for(int i = 0; i < 4; ++i)
				m_outPorts[i] = jack_port_register(m_client, ("out_" + std::to_string(i + 1)).c_str(), JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
			m_inPorts[0] = jack_port_register(m_client, "in_L", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
			m_inPorts[1] = jack_port_register(m_client, "in_R", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
			jack_set_process_callback(m_client, [](jack_nframes_t _n, void* _arg) { return static_cast<JackAudio*>(_arg)->process(_n); }, this);
			if(jack_activate(m_client))
			{
				jack_client_close(m_client);
				m_client = nullptr;
				return;
			}
			if(_autoConnect)
				if(const char** ports = jack_get_ports(m_client, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsPhysical | JackPortIsInput))
				{
					for(int i = 0; i < 2 && ports[i]; ++i)
						jack_connect(m_client, jack_port_name(m_outPorts[i]), ports[i]);
					jack_free(ports);
				}
		}

		~JackAudio()
		{
			if(m_client)
			{
				jack_deactivate(m_client);
				jack_client_close(m_client);
			}
		}

		bool valid() const { return m_client != nullptr; }
		uint32_t rate() const { return m_bridge.rate(); }
		uint64_t xruns() const { return m_bridge.xruns(); }
		float peak() { return m_bridge.peak(); }
		void setGain(const float _gain) { m_bridge.setGain(_gain); }
		void push(int32_t _o1, int32_t _o2, int32_t _o3, int32_t _o4) { m_bridge.push(_o1, _o2, _o3, _o4); }
		void pullInput(int32_t& _l, int32_t& _r) { m_bridge.pullInput(_l, _r); }

	private:
		int process(const jack_nframes_t _n)
		{
			std::array<float*, AudioBridge::Outs> out{};
			for(size_t i = 0; i < out.size(); ++i)
				out[i] = static_cast<float*>(jack_port_get_buffer(m_outPorts[i], _n));
			std::array<const float*, AudioBridge::Ins> in{};
			for(size_t i = 0; i < in.size(); ++i)
				in[i] = static_cast<const float*>(jack_port_get_buffer(m_inPorts[i], _n));

			m_bridge.render(out.data(), out.size(), in.data(), in.size(), _n);
			return 0;
		}

		AudioBridge m_bridge;
		jack_client_t* m_client = nullptr;
		std::array<jack_port_t*, AudioBridge::Outs> m_outPorts{};
		std::array<jack_port_t*, AudioBridge::Ins> m_inPorts{};
	};
}
