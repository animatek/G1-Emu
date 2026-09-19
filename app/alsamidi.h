#pragma once

// Virtual ALSA MIDI ports (sequencer). Every port is input and output at the same time
// and shows up in any MIDI program on the system (NME, Bitwig, aconnect).
// It works on raw bytes: incoming events are turned into MIDI bytes and outgoing bytes are
// split into events with ALSA's encoder, SysEx included.

#include <alsa/asoundlib.h>

#include <cstdint>
#include <string>
#include <vector>

namespace g1app
{
	class AlsaMidi
	{
	public:
		explicit AlsaMidi(const char* _clientName)
		{
			if(snd_seq_open(&m_seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK) < 0)
			{
				m_seq = nullptr;
				return;
			}
			snd_seq_set_client_name(m_seq, _clientName);
			snd_midi_event_new(4096, &m_decoder);
			snd_midi_event_no_status(m_decoder, 1);
		}

		~AlsaMidi()
		{
			for(auto* e : m_encoders)
				snd_midi_event_free(e);
			if(m_decoder)
				snd_midi_event_free(m_decoder);
			if(m_seq)
				snd_seq_close(m_seq);
		}

		bool valid() const { return m_seq != nullptr; }
		int clientId() const { return m_seq ? snd_seq_client_id(m_seq) : -1; }

		// Creates a port and returns its index.
		int addPort(const char* _name)
		{
			const int port = snd_seq_create_simple_port(m_seq, _name,
				SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
				SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
			m_ports.push_back(port);
			snd_midi_event_t* enc = nullptr;
			snd_midi_event_new(4096, &enc);
			m_encoders.push_back(enc);
			return static_cast<int>(m_ports.size()) - 1;
		}

		// Collects everything that has arrived, split by port.
		void poll(std::vector<std::vector<uint8_t>>& _perPort)
		{
			_perPort.resize(m_ports.size());
			snd_seq_event_t* ev = nullptr;
			while(snd_seq_event_input(m_seq, &ev) >= 0 && ev)
			{
				int index = -1;
				for(size_t i = 0; i < m_ports.size(); ++i)
					if(m_ports[i] == ev->dest.port)
						index = static_cast<int>(i);
				if(index < 0)
					continue;
				auto& dst = _perPort[static_cast<size_t>(index)];
				if(ev->type == SND_SEQ_EVENT_SYSEX)
				{
					const auto* p = static_cast<const uint8_t*>(ev->data.ext.ptr);
					dst.insert(dst.end(), p, p + ev->data.ext.len);
					continue;
				}
				uint8_t buf[16];
				snd_midi_event_reset_decode(m_decoder);
				const long n = snd_midi_event_decode(m_decoder, buf, sizeof(buf), ev);
				if(n > 0)
					dst.insert(dst.end(), buf, buf + n);
			}
		}

		// Sends raw bytes through a port (grouped into complete events).
		void send(const int _index, const std::vector<uint8_t>& _bytes)
		{
			if(_bytes.empty())
				return;
			auto* enc = m_encoders[static_cast<size_t>(_index)];
			for(const auto b : _bytes)
			{
				snd_seq_event_t ev;
				snd_seq_ev_clear(&ev);
				if(snd_midi_event_encode_byte(enc, b, &ev) != 1)
					continue;
				snd_seq_ev_set_source(&ev, m_ports[static_cast<size_t>(_index)]);
				snd_seq_ev_set_subs(&ev);
				snd_seq_ev_set_direct(&ev);
				snd_seq_event_output_direct(m_seq, &ev);
			}
		}

	private:
		snd_seq_t* m_seq = nullptr;
		snd_midi_event_t* m_decoder = nullptr;
		std::vector<int> m_ports;
		std::vector<snd_midi_event_t*> m_encoders;
	};
}
