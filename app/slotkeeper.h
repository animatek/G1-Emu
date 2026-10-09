#pragma once

// SlotKeeper: what the G1 holds in each of its four slots, kept outside the G1 so a DAW project can
// bring it back (issue #25).
//
// The flash keeps the stored banks, but a slot holds whatever was put there last: a patch sent by
// an editor, one loaded from the panel or by a Program Change, and every edit since. None of that
// is in the flash. So the keeper asks the OS for it, the way an editor does (RequestPatch, then the
// 13 GetPatch sections), whenever a slot may have changed and the PC Port has been quiet for a
// while; and after a restore it uploads it again, the way an editor does too (packets, each one
// waiting for its ACK). It speaks to the OS through the same PC Port as the editor, between the
// editor's messages, and hides its own traffic from the editor.
//
// It runs no thread and owns no G1: the runner feeds it, under the engine lock, what the editor
// sent, what the G1 sent and the G1's time, and gives the G1 what the keeper wants to send. A
// fetched slot is kept as the sections an upload carries (PatchName, Header, ModuleDump... in
// NME's order), so restoring it needs no patch parser.

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace g1app
{
	class SlotKeeper
	{
	public:
		static constexpr size_t SlotCount = 4;

		// One slot: its sections in upload order, or none if the slot holds nothing known.
		struct Slot
		{
			std::vector<std::vector<uint8_t>> sections;
			bool empty() const { return sections.empty(); }
			bool operator==(const Slot& _o) const { return sections == _o.sections; }
		};
		using Slots = std::array<Slot, SlotCount>;

		// Worker thread, with the engine lock held. _nowMs is the G1's own time (its CPU cycles).
		void editorSent(const std::vector<uint8_t>& _bytes, uint64_t _nowMs);	// already given to the G1
		void g1Sent(const std::vector<uint8_t>& _bytes, uint64_t _nowMs, std::vector<uint8_t>& _toEditor);
		void tick(uint64_t _nowMs, std::vector<uint8_t>& _toG1);

		// Any thread. What the slots hold, as far as the keeper knows; restore() sends them to the
		// G1 once it has booted (empty slots are left alone) and then keeps them as known.
		Slots slots() const;
		void restore(const Slots& _slots);
		// _slot may have changed in a way the OS tells no one (the System menu's patch settings):
		// read it again once the PC Port is quiet.
		void reread(size_t _slot);
		// True while nothing is waiting to be fetched or uploaded: slots() is what the G1 has.
		bool settled() const;

		// The slots as bytes for a project, and back (false if they are not this format).
		static std::vector<uint8_t> pack(const Slots& _slots);
		static bool unpack(const std::vector<uint8_t>& _bytes, Slots& _slots);

		// What the OS answers to the 13 GetPatch requests (7-bit unpacked), as the sections an
		// upload carries. Public for the tests. False if the replies are not what was expected.
		static bool repliesToSections(const std::vector<std::vector<uint8_t>>& _replies, std::vector<std::vector<uint8_t>>& _sections);
		// An upload of those sections to _slot: the SysEx messages, each to be sent after the last
		// one's ACK.
		static std::vector<std::vector<uint8_t>> uploadMessages(const std::vector<std::vector<uint8_t>>& _sections, uint8_t _slot);

	private:
		enum class State { Boot, Greeting, Idle, Requesting, Fetching, Uploading };

		void send(std::vector<uint8_t> _msg, uint64_t _nowMs, std::vector<uint8_t>& _toG1);
		void handle(const std::vector<uint8_t>& _msg, uint64_t _nowMs);
		void abort(uint64_t _nowMs);
		void finishFetch();

		mutable std::mutex m_mutex;		// m_slots, m_pendingRestore, m_dirty, m_uploadPending, m_state for other threads
		Slots m_slots;
		Slots m_pendingRestore;
		bool m_hasRestore = false;
		std::array<bool, SlotCount> m_dirty{true, true, true, true};	// fetch all once at boot

		std::atomic<State> m_state{State::Boot};
		std::array<bool, SlotCount> m_uploadPending{};	// restored slots not yet sent to the G1
		uint64_t m_lastActivity = 0;	// the editor's last message, or the G1's last change report
		bool m_editorUploading = false;	// an upload from the editor has begun and not ended
		uint64_t m_deadline = 0;		// when the transaction in flight is given up
		uint64_t m_filterUntil = 0;		// replies to the keeper are hidden from the editor until then
		size_t m_slot = 0;				// the slot being fetched or uploaded
		uint8_t m_pid = 0;
		size_t m_step = 0;				// GetPatch request or upload packet in flight
		std::vector<std::vector<uint8_t>> m_replies;
		std::vector<uint8_t> m_section;	// a reply's packets so far (7-bit)
		std::vector<std::vector<uint8_t>> m_upload;
		std::vector<uint8_t> m_rx;		// the G1's bytes not yet a whole message
		std::vector<uint8_t> m_editorRx;
	};
}
