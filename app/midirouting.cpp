#include "midirouting.h"

#include "synthsettings.h"
#include "g1Lib/g1knobs.h"

#include <algorithm>

namespace g1app
{
	namespace
	{
		constexpr uint32_t OctShiftAddr = 0x1c3ab8;	// + slot, + the OS's shift (KnobMap::osShift)
		constexpr uint8_t SlotLedBit = 0x80;			// slot s's LED: bit 7 of LED row s (active low)
		constexpr uint8_t BankSelectCc = 0, ModWheelCc = 1, BankSelectLsbCc = 32, SustainCc = 64, ChannelModeCc = 120;

		// The data bytes a channel message has after its status.
		int dataBytes(const uint8_t _status)
		{
			const auto type = _status & 0xf0;
			return type == 0xc0 || type == 0xd0 ? 1 : 2;
		}
	}

	int MidiRouting::shift(g1::Microcontroller& _mc, const size_t _slot)
	{
		g1::KnobMap map(_mc);
		const auto v = static_cast<int8_t>(_mc.read8(OctShiftAddr + map.osShift() + static_cast<uint32_t>(_slot & 3)));
		return std::clamp<int>(v, -MaxShift, MaxShift);
	}

	void MidiRouting::setShift(g1::Microcontroller& _mc, const size_t _slot, const int _shift)
	{
		g1::KnobMap map(_mc);
		_mc.write8(OctShiftAddr + map.osShift() + static_cast<uint32_t>(_slot & 3), static_cast<uint8_t>(std::clamp(_shift, -MaxShift, MaxShift)));
	}


	MidiRouting::Layout MidiRouting::layout(const SynthSettingsLink* _settings, const bool _selectedControllers, const bool _octaveShift)
	{
		SynthSettings s;
		uint64_t revision = 0;
		if(_settings)
			_settings->settings(s, revision);	// left as the defaults until the OS has said them
		Layout l;
		l.channels = s.midiChannel;
		l.selectedSlots = s.keyboardMode == 1;
		l.selectedControllers = _selectedControllers;
		l.octaveShift = _octaveShift;
		return l;
	}

	bool MidiRouting::lastController(int& _channel, int& _cc, uint32_t& _serial) const
	{
		_serial = m_controllerSerial.load(std::memory_order_acquire);
		const int last = m_lastController.load(std::memory_order_relaxed);
		_channel = last >> 8;
		_cc = last & 0x7f;
		return _serial > 0;
	}

	void MidiRouting::apply(g1::Microcontroller& _mc, const Layout& _layout, const uint8_t* _in, const size_t _size, std::vector<uint8_t>& _out)
	{
		for(size_t i = 0; i < _size; ++i)
		{
			const auto b = _in[i];
			if(b >= 0xf8)
			{
				_out.push_back(b);	// real time: anywhere, and it leaves the running status alone
				continue;
			}
			if(b >= 0xf0)
			{
				_out.push_back(b);	// system common and SysEx: no running status after them
				m_sysex = b == 0xf0;
				m_status = 0;
				m_count = 0;
				continue;
			}
			if(b & 0x80)
			{
				m_sysex = false;
				m_status = b;
				m_count = 0;
				continue;
			}
			if(m_sysex || !m_status)
			{
				_out.push_back(b);	// SysEx data, or data with no status to go with it
				continue;
			}
			m_data[m_count++] = b;
			if(m_count == dataBytes(m_status))
			{
				message(_mc, _layout, _out);
				m_count = 0;
			}
		}
	}

	// Where a message on _channel goes: to that channel, moved by the shift of the slot on it (the
	// active one, if more than one is). _layered (Selected Slots) and _channel a selected slot's: to each
	// selected slot's own channel, by its own shift, whichever of them is active (as the keyboard
	// model's keys play them all); a channel two of them share once, by the active slot's shift if
	// it is one of them.
	int MidiRouting::targets(g1::Microcontroller& _mc, const Layout& _layout, const int _channel, const bool _layered, std::array<Target, MaxCopies>& _out) const
	{
		const auto active = static_cast<int>(_mc.read8(g1::KnobMap::ActiveSlot + g1::KnobMap(_mc).osShift()) & 3);
		int count = 0;
		const auto add = [&](const int _ch, const int _slot)
		{
			for(int i = 0; i < count; ++i)
				if(_out[static_cast<size_t>(i)].channel == _ch)
					return;
			_out[static_cast<size_t>(count++)] = {static_cast<uint8_t>(_ch), _slot};
		};
		if(_layered)
		{
			// The selected slots: the active one (its LED blinks), and those whose LED is lit.
			std::array<bool, 4> selected{};
			bool onSelected = false;
			for(int s = 0; s < 4; ++s)
			{
				selected[static_cast<size_t>(s)] = s == active || !(_mc.ledRow(static_cast<uint32_t>(s)) & SlotLedBit);	// active low
				onSelected = onSelected || (selected[static_cast<size_t>(s)] && _layout.channels[static_cast<size_t>(s)] == _channel);
			}
			if(onSelected)
			{
				if(const int ch = _layout.channels[static_cast<size_t>(active)]; ch >= 0 && ch < 16)
					add(ch, active);	// first: a channel it shares goes by its shift
				for(int s = 0; s < 4; ++s)
				{
					const int ch = _layout.channels[static_cast<size_t>(s)];
					if(selected[static_cast<size_t>(s)] && ch >= 0 && ch < 16)
						add(ch, s);
				}
				return count;
			}
		}
		int slot = _layout.channels[static_cast<size_t>(active)] == _channel ? active : -1;
		for(int s = 0; s < 4 && slot < 0; ++s)
			if(_layout.channels[static_cast<size_t>(s)] == _channel)
				slot = s;
		add(_channel, slot);
		return count;
	}

	// A whole channel message, sent with its status each time: what is left out (a note moved off
	// the keyboard) never breaks a running status the G1 would follow.
	void MidiRouting::message(g1::Microcontroller& _mc, const Layout& _layout, std::vector<uint8_t>& _out)
	{
		const auto type = static_cast<uint8_t>(m_status & 0xf0);
		const int channel = m_status & 0x0f;
		const auto send = [&](const int _ch, const uint8_t _d0)
		{
			_out.push_back(static_cast<uint8_t>(type | _ch));
			_out.push_back(_d0);
			if(dataBytes(m_status) == 2)
				_out.push_back(m_data[1]);
		};

		std::array<Target, MaxCopies> to{};
		if(type == 0xb0)
		{
			m_lastController.store((channel << 8) | m_data[0], std::memory_order_relaxed);
			m_controllerSerial.fetch_add(1, std::memory_order_release);
		}
		if(type != 0x80 && type != 0x90 && type != 0xa0)
		{
			// What goes with the notes (and the channel mode messages, All Notes Off...) goes to every
			// slot playing them; with MIDI CC Mode on Selected Slots, every controller does too, but Bank
			// Select, which goes with a Program Change on its channel alone. The rest stays on its
			// channel.
			const bool controller = type == 0xb0;
			const auto cc = m_data[0];	// a controller's number
			const bool withNotes = type == 0xd0 || type == 0xe0 || (controller && (cc == ModWheelCc || cc == SustainCc || cc >= ChannelModeCc));
			const bool layered = (withNotes && _layout.selectedSlots)
				|| (controller && _layout.selectedControllers && cc != BankSelectCc && cc != BankSelectLsbCc);
			const int n = layered ? targets(_mc, _layout, channel, true, to) : 0;
			if(n < 2)
				send(channel, m_data[0]);
			else
				for(int i = 0; i < n; ++i)
					send(to[static_cast<size_t>(i)].channel, m_data[0]);
			return;
		}

		const auto note = m_data[0];
		auto& sent = m_sent[static_cast<size_t>(channel)][note];
		const bool on = type == 0x90 && m_data[1] > 0;
		const bool off = type == 0x80 || (type == 0x90 && m_data[1] == 0);
		if(on || !sent.played)
		{
			// Where the note goes now: each target moved by its slot's shift, what goes off the
			// keyboard left out. A note on keeps it for its note off and its pressure.
			const int n = targets(_mc, _layout, channel, _layout.selectedSlots, to);
			Sent now;
			now.played = on;
			for(int i = 0; i < n; ++i)
			{
				const auto& t = to[static_cast<size_t>(i)];
				const int moved = note + (t.slot < 0 || !_layout.octaveShift ? 0 : 12 * shift(_mc, static_cast<size_t>(t.slot)));
				if(moved < 0 || moved > 127)
					continue;
				now.channel[static_cast<size_t>(now.count)] = t.channel;
				now.note[static_cast<size_t>(now.count++)] = static_cast<uint8_t>(moved);
			}
			for(int i = 0; i < now.count; ++i)
				send(now.channel[static_cast<size_t>(i)], now.note[static_cast<size_t>(i)]);
			if(on)
				sent = now;
			return;
		}
		// A note off, or the pressure of a held note: where its note on went, whatever the shifts
		// and the selection are now.
		for(int i = 0; i < sent.count; ++i)
			send(sent.channel[static_cast<size_t>(i)], sent.note[static_cast<size_t>(i)]);
		if(off)
			sent = {};
	}
}
