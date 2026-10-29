#pragma once

// A Micro Modular in its update mode, for testing an OS update from an editor (NME's Device > Update
// Synth OS) without the real thing. The real Micro keeps its update utility in a boot ROM that
// Clavia's updater does not carry and G1-Emu does not have, so this is not that code: it is what the
// utility is known to do, from Clavia's updater's own text and from the rack's utility, which the
// Micro is assumed to share the stream with (not checked: no capture of a real Micro was ever made).
//
//   power-on with Shift held: the display reads "Ud"
//   the editor's greeting is answered with an IAm that says update mode (7F where the OS puts its
//   serial's top bits); then 70 (header), 71 (112 data bytes in 128), 73 (the sum of the data);
//   a good sum is answered F0 33 00 06 7F F7, and the display flashes "St"
//   one of the two patch buttons stores the OS and the display reads "rd" (Clavia's updater says so); Shift cancels
//
// The stream is the rack's (g1update.py, OsUpdate.h in NME): every message F0 33 7F 06 <cmd> ... F7,
// the data scrambled (swapped bit pairs, XORed with 17 growing by 17) and packed 7 bytes into 8.
// What is stored is the image that runs from RAM at $100000; EmuHost keeps it as the Micro's OS
// (`microOs` in the settings) when the G1 is restarted, as it does for the rack's update.

#include <array>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace g1app
{
	class MicroUpdate
	{
	public:
		enum class State { Waiting, Receiving, Ready, Stored, Failed };

		// What the editor sent (any split of the stream); what the "synth" answers goes on _reply.
		void feed(const std::vector<uint8_t>& _bytes, std::vector<uint8_t>& _reply);

		// A patch button: stores the OS when the update is ready. Shift: cancels it, and the OS stays as it was.
		void press();
		void cancel();

		State state() const;
		std::string describe() const;		// one line for the log and the status

		// The two digits as the OS drives them (a segment is lit when its bit is low, a-g bits 0-6): index 1 is the left one.
		std::array<uint8_t, 2> digits(uint64_t _nowMs) const;

		// The OS, once stored.
		bool storedImage(std::vector<uint8_t>& _os) const;

		static constexpr size_t BlockBytes = 112;

	private:
		void message(const std::vector<uint8_t>& _m, std::vector<uint8_t>& _reply);
		mutable std::mutex m_mutex;
		State m_state = State::Waiting;
		std::vector<uint8_t> m_pending;		// bytes of a message not finished yet
		std::vector<uint8_t> m_data;		// the scrambled OS as it came
		uint32_t m_length = 0, m_address = 0;
		uint32_t m_sum = 0;
		std::string m_why;
		std::vector<uint8_t> m_image;
	};
}
