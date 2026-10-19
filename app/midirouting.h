#pragma once

// What the hosts do to the MIDI on its way to the G1's MIDI IN, where the rack's OS alone would
// not do what the keyboard model does.
//
// The octave shift of each slot (-2 to +2). The rack's OS keeps one per slot at $1C3AB8 + slot
// and carries it in the patch (3 bits of the header), and its Oct down/up keys (2.4 and 2.5) step
// the active slot's, but nothing in its note path reads it: only the keyboard model's own keys
// would move (NOTES.md, "Oct Shift is the keyboard model's"). So the hosts apply it to the notes
// they give the G1: a note on a slot's MIDI channel is moved by that slot's shift, its note off
// follows it wherever the shift has gone since, and a note moved off the keyboard (0-127) is not
// played. Two slots on one channel take the active slot's shift: from outside the OS a note
// cannot be split between them. Octave Shift, the hosts' own setting, Off leaves the notes as the
// rack plays them: a patch made on the keyboard model with a shift plays as on the rack.
//
// Keyboard Mode set to Selected Slots (the synth settings), which on the keyboard model layers
// the selected slots on its own keys and on the rack only the editor's Keyboard Floater: a note
// on any selected slot's channel is played by every selected slot, whichever is active, each on
// its own channel and moved by its own shift, and what goes with the notes (sustain, pitch bend,
// the mod wheel, aftertouch, the channel mode messages) goes to them all too. Other controllers
// stay on their channel, as on the hardware (each slot receives on its own channel), unless MIDI
// CC Mode, the hosts' own setting, is Selected Slots: then every controller on a selected slot's
// channel goes to each selected slot, whatever Keyboard Mode is, and each patch answers its own
// assignments. Bank Select never: it goes with a Program Change, which stays on its channel.
//
// It runs on the thread that runs the G1 (EmuHost's, the plugin's Runner), which reads the
// shifts, the active slot and the selected ones (their LEDs) straight from the G1.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace g1
{
	class Microcontroller;
}

namespace g1app
{
	class SynthSettingsLink;

	class MidiRouting
	{
	public:
		static constexpr int MaxShift = 2;

		// _slot's shift (0-3) as the OS keeps it, -2 to +2. setShift() writes it where the Oct keys
		// do: the patch carries it from then on. Where the G1 runs, or under its engine lock.
		static int shift(g1::Microcontroller& _mc, size_t _slot);
		static void setShift(g1::Microcontroller& _mc, size_t _slot, int _shift);

		// What the synth settings say about where notes go: the slots' MIDI channels (0-15, 16 =
		// off) and Keyboard Mode. layout() has them as _settings last read them from the OS (its
		// defaults, channels 1 to 4 and Active Slot, until then or without it), and MIDI CC Mode and
		// Octave Shift, the host's own settings (no OS has them).
		struct Layout
		{
			std::array<int, 4> channels{0, 1, 2, 3};
			bool selectedSlots = false;			// Keyboard Mode: Selected Slots
			bool selectedControllers = false;	// MIDI CC Mode: Selected Slots
			bool octaveShift = false;			// Octave Shift: On, the notes moved by their slot's shift
		};
		static Layout layout(const SynthSettingsLink* _settings, bool _selectedControllers, bool _octaveShift);

		// _in, on its way to the G1's MIDI IN, into _out with each note moved by its slot's shift,
		// and with Selected Slots, played by each selected slot.
		void apply(g1::Microcontroller& _mc, const Layout& _layout, const uint8_t* _in, size_t _size, std::vector<uint8_t>& _out);

		// Any thread: the last controller that came in (for the panel's MIDI Learn), and a serial
		// that grows with each one. False until one has.
		bool lastController(int& _channel, int& _cc, uint32_t& _serial) const;

	private:
		static constexpr int MaxCopies = 4;
		struct Target { uint8_t channel; int slot; };	// a copy of a message: its channel, the slot whose shift moves it (-1: none)
		struct Sent											// what a note played went to
		{
			bool played = false;	// a note on is held (its copies may all have been moved off the keyboard)
			int count = 0;
			std::array<uint8_t, MaxCopies> channel{}, note{};
		};

		void message(g1::Microcontroller& _mc, const Layout& _layout, std::vector<uint8_t>& _out);
		int targets(g1::Microcontroller& _mc, const Layout& _layout, int _channel, bool _layered, std::array<Target, MaxCopies>& _out) const;

		uint8_t m_status = 0;		// running status: the last channel message's
		bool m_sysex = false;		// between $F0 and $F7, everything goes through
		uint8_t m_data[2] = {};
		int m_count = 0;
		std::array<std::array<Sent, 128>, 16> m_sent{};	// per channel and note played
		std::atomic<uint32_t> m_controllerSerial{0};
		std::atomic<int> m_lastController{0};			// channel << 8 | CC number
	};
}
