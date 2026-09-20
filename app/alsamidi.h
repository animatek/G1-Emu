#pragma once

// Virtual ALSA MIDI ports (sequencer). Every port is input and output at the same time
// and is visible to ALSA sequencer clients (NME, aconnect). Raw MIDI clients such as
// Bitwig see no sequencer port at all: for them the emulator takes over the snd-virmidi
// card and links it to its own ports itself (link(), see docs/bitwig-midi.md).
// It works on raw bytes: incoming events are turned into MIDI bytes and outgoing bytes are
// split into events with ALSA's encoder, SysEx included.

#include <alsa/asoundlib.h>

#include <cstdint>
#include <string>
#include <utility>
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

		// The sequencer ports ALSA publishes for a sound card, as (client, port) pairs in the
		// order it lists them, with the name it gives each one. Going by the card and not by the
		// client's name is what lets any card do the job: snd-virmidi calls its clients "Virtual
		// Raw MIDI <card>-<device>", a USB MIDI gadget calls them whatever its product string says.
		struct Port { int client = -1, port = -1; std::string name; };

		std::vector<Port> findCardPorts(const int _card) const
		{
			std::vector<Port> out;
			if(!m_seq || _card < 0)
				return out;
			snd_seq_client_info_t* client = nullptr;
			snd_seq_port_info_t* port = nullptr;
			snd_seq_client_info_alloca(&client);
			snd_seq_port_info_alloca(&port);
			snd_seq_client_info_set_client(client, -1);
			while(snd_seq_query_next_client(m_seq, client) >= 0)
			{
				const int id = snd_seq_client_info_get_client(client);
				if(id == snd_seq_client_id(m_seq) || snd_seq_client_info_get_card(client) != _card)
					continue;
				snd_seq_port_info_set_client(port, id);
				snd_seq_port_info_set_port(port, -1);
				while(snd_seq_query_next_port(m_seq, port) >= 0)
				{
					const char* n = snd_seq_port_info_get_name(port);
					out.push_back({id, snd_seq_port_info_get_port(port), n ? n : ""});
				}
			}
			return out;
		}

		// Subscribes one of our ports to another one in both directions (their output to our
		// input and our output to their input). An existing subscription counts as done.
		bool link(const int _index, const int _client, const int _port)
		{
			if(!m_seq || _index < 0 || static_cast<size_t>(_index) >= m_ports.size())
				return false;
			const snd_seq_addr_t self{static_cast<unsigned char>(snd_seq_client_id(m_seq)), static_cast<unsigned char>(m_ports[static_cast<size_t>(_index)])};
			const snd_seq_addr_t other{static_cast<unsigned char>(_client), static_cast<unsigned char>(_port)};
			return subscribe(other, self) && subscribe(self, other);
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
		bool subscribe(const snd_seq_addr_t& _from, const snd_seq_addr_t& _to) const
		{
			snd_seq_port_subscribe_t* sub = nullptr;
			snd_seq_port_subscribe_alloca(&sub);
			snd_seq_port_subscribe_set_sender(sub, &_from);
			snd_seq_port_subscribe_set_dest(sub, &_to);
			const int r = snd_seq_subscribe_port(m_seq, sub);
			return r == 0 || r == -EBUSY;	// EBUSY: it was already subscribed
		}

		snd_seq_t* m_seq = nullptr;
		snd_midi_event_t* m_decoder = nullptr;
		std::vector<int> m_ports;
		std::vector<snd_midi_event_t*> m_encoders;
	};
}
