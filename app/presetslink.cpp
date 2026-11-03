#include "presetslink.h"
#include "pcsysex.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace g1app
{
	using namespace pcsysex;

	namespace
	{
		constexpr uint64_t BootMs = 2500;		// the OS is up and has looked at its flash
		constexpr uint64_t QuietMs = 500;		// the editor's own exchanges come closer together
		constexpr uint64_t LoadQuietMs = 100;	// a load or an upload the user waits for: no more than this
		constexpr uint64_t ReplyMs = 1500;
		constexpr uint64_t FilterTailMs = 300;	// late replies to an abandoned request
		constexpr uint8_t CcIAm = 0x00, CcInfo = 0x14, CcAck = 0x16, CcPatch = 0x17;
		constexpr uint8_t LoadAck = 0x38, InfoNewPatchInSlot = 0x38;
		constexpr uint8_t ListAckA = 0x13, ListAckB = 0x15;	// the ACK types that carry a list
		constexpr uint8_t ListEnd = 4;		// the end marker that says the whole list is done
		constexpr uint8_t PacketAck = 0x36, LastPacketAck = 0x7f;	// an upload's packet taken
		constexpr uint64_t PacketMs = 3000;	// the OS takes longer at some packets (it reloads the DSPs)
		constexpr uint8_t PatchIdAck = 0x36;	// RequestPatch's answer: the patch's id after it
		constexpr uint8_t GetControlMap = 0x61, ControlMapType = 96;
		constexpr uint8_t AssignCtrl = 0x22, ChangeCtrl = 0x23;	// MidiCtrlAssignment, and its change or removal

		// G1_PRESETS_TRACE=1: every message the link sends and every one it keeps, on stderr.
		void traceMsg(const char* _dir, const uint64_t _nowMs, const std::vector<uint8_t>& _m)
		{
			static const bool on = std::getenv("G1_PRESETS_TRACE") != nullptr;
			if(!on)
				return;
			std::fprintf(stderr, "[presets %6llu ms] %s", static_cast<unsigned long long>(_nowMs), _dir);
			for(const auto b : _m)
				std::fprintf(stderr, " %02x", b);
			std::fprintf(stderr, "\n");
		}
	}

	// ____________________________________________________________________________________________
	// The answer: three bytes of no known use, then names, each perhaps after a code that moves
	// the cursor (1: to a position; 2: one position on, an empty one; 3: to a bank and position;
	// 5: an older location, three bytes, of no use here), and an end marker (4: the list is over).
	// A name is up to 16 characters, ended by a zero only when shorter: reading past 16 would run
	// into the next name. As Animatek NME decodes it (source/midi/NmMessages.cpp).
	std::vector<PresetsLink::Entry> PresetsLink::decodeList(const std::vector<uint8_t>& _c, int _bank, int _position, int& _nextBank, int& _nextPosition)
	{
		std::vector<Entry> entries;
		_nextBank = _nextPosition = -1;
		if(_c.size() < 4)
			return entries;
		const size_t end = _c.size() - 1;		// the end marker
		const int marker = _c[end] & 0x7f;
		size_t p = 3;
		while(p < end)
		{
			const int code = _c[p] & 0x7f;
			if(code == 1)
			{
				if(++p < end)
					_position = _c[p++] & 0x7f;
			}
			else if(code == 2)
			{
				++p;
				++_position;
			}
			else if(code == 3)
			{
				++p;
				if(p + 1 < end)
				{
					_bank = _c[p++] & 0x7f;
					_position = _c[p++] & 0x7f;
				}
			}
			else if(code == 5)
				p = std::min(end, p + 4);
			if(p >= end)
				break;
			std::string name;
			while(p < end && _c[p] != 0 && name.size() < 16)
				name += static_cast<char>(_c[p++]);
			if(p < end && _c[p] == 0)
				++p;
			entries.push_back({_bank, _position, name});
			++_position;
		}
		if(marker == ListEnd)
			return entries;
		if(_position >= Positions)
		{
			_position = 0;
			++_bank;
		}
		if(_bank < Banks)
		{
			_nextBank = _bank;
			_nextPosition = _position;
		}
		return entries;
	}

	// ____________________________________________________________________________________________
	// Any thread

	void PresetsLink::readBank(const int _bank)
	{
		if(_bank < 0 || _bank >= Banks)
			return;
		std::lock_guard<std::mutex> lock(m_mutex);
		m_readWanted[static_cast<size_t>(_bank)] = true;
	}

	void PresetsLink::load(const int _slot, const int _bank, const int _position)
	{
		if(_slot < 0 || _slot > 3 || _bank < 0 || _bank >= Banks || _position < 0 || _position >= Positions)
			return;
		std::lock_guard<std::mutex> lock(m_mutex);
		m_loads.push_back({_slot, _bank, _position});
	}

	void PresetsLink::readControls(const int _slot)
	{
		if(_slot < 0 || _slot > 3)
			return;
		std::lock_guard<std::mutex> lock(m_mutex);
		m_controlJobs.push_back({_slot, false});
	}

	void PresetsLink::assignControl(const int _slot, const int _section, const int _module, const int _param, const int _ctrl, const int _previous)
	{
		if(_slot < 0 || _slot > 3 || (_ctrl < 0 && _previous < 0))
			return;
		std::lock_guard<std::mutex> lock(m_mutex);
		m_controlJobs.push_back({_slot, true, _section, _module, _param, _ctrl, _previous});
	}

	bool PresetsLink::controls(const int _slot, std::vector<Control>& _out, uint64_t& _revision) const
	{
		if(_slot < 0 || _slot > 3)
			return false;
		std::lock_guard<std::mutex> lock(m_mutex);
		const auto& c = m_controls[static_cast<size_t>(_slot)];
		_out = c.list;
		_revision = c.revision;
		return c.known;
	}

	std::vector<PresetsLink::Control> PresetsLink::decodeControls(const std::vector<uint8_t>& _section)
	{
		std::vector<Control> list;
		if(_section.empty() || _section[0] != ControlMapType)
			return list;
		size_t bit = 8;
		const auto read = [&](const int _bits)
		{
			int v = 0;
			for(int i = 0; i < _bits; ++i, ++bit)
				v = (v << 1) | (bit / 8 < _section.size() ? (_section[bit / 8] >> (7 - bit % 8)) & 1 : 0);
			return v;
		};
		const int count = read(7);
		for(int i = 0; i < count && (bit + 23) / 8 <= _section.size(); ++i)
		{
			Control c;
			c.ctrl = read(7);
			c.section = read(2);
			c.module = read(7);
			c.param = read(7);
			list.push_back(c);
		}
		return list;
	}

	void PresetsLink::upload(const int _slot, std::vector<std::vector<uint8_t>> _frames, std::vector<uint8_t> _abort, const int _bank, const int _position)
	{
		if(_slot < 0 || _slot > 3 || _frames.empty())
			return;
		std::lock_guard<std::mutex> lock(m_mutex);
		Upload u;
		u.slot = _slot;
		u.frames = std::move(_frames);
		u.abort = std::move(_abort);
		u.bank = _bank >= 0 && _bank < Banks && _position >= 0 && _position < Positions ? _bank : -1;
		u.position = _position;
		m_uploads.push_back(std::move(u));
		m_uploadStatus.busy = true;
		m_uploadStatus.message = "Sending the patch to the synth...";
		++m_uploadStatus.serial;
		++m_revision;
	}

	void PresetsLink::storeSlot(const int _slot, const int _bank, const int _position)
	{
		if(_slot < 0 || _slot > 3 || _bank < 0 || _bank >= Banks || _position < 0 || _position >= Positions)
			return;
		std::lock_guard<std::mutex> lock(m_mutex);
		Upload u;			// an upload of nothing: only the Store at its end
		u.slot = _slot;
		u.bank = _bank;
		u.position = _position;
		m_uploads.push_back(std::move(u));
		m_uploadStatus.busy = true;
		m_uploadStatus.message = "Storing slot " + std::string(1, static_cast<char>('A' + _slot)) + "...";
		++m_uploadStatus.serial;
		++m_revision;
	}

	PresetsLink::UploadStatus PresetsLink::uploadStatus() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_uploadStatus;
	}

	bool PresetsLink::bank(const int _bank, BankNames& _out) const
	{
		if(_bank < 0 || _bank >= Banks)
			return false;
		std::lock_guard<std::mutex> lock(m_mutex);
		_out = m_names[static_cast<size_t>(_bank)];
		return m_known[static_cast<size_t>(_bank)];
	}

	bool PresetsLink::reading(const int _bank) const
	{
		if(m_readingBank.load() == _bank)
			return true;
		std::lock_guard<std::mutex> lock(m_mutex);
		return _bank >= 0 && _bank < Banks && m_readWanted[static_cast<size_t>(_bank)];
	}

	void PresetsLink::reset()
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_names = {};
		m_known = {};
		m_readWanted = {};
		m_loads.clear();
		m_uploads.clear();
		m_uploadStatus = {};
		m_uploadNext = false;
		m_controlJobs.clear();
		m_controls = {};
		m_controlNext = false;
		m_controlPackets.clear();
		m_state = State::Idle;
		m_readingBank = -1;
		m_deadline = m_filterUntil = m_lastActivity = 0;
		m_rx.clear();
		m_editorRx.clear();
		m_toEditor.clear();
		++m_revision;
	}

	// ____________________________________________________________________________________________
	// Worker thread

	// _answered: the request got its answer, so no late one is coming and nothing more is kept
	// from the editor; otherwise (no answer, or abandoned) its late answers are, for a moment.
	void PresetsLink::finish(const uint64_t _nowMs, const bool _answered)
	{
		m_state = State::Idle;
		m_pendingNext = false;
		m_uploadNext = false;
		m_controlNext = false;
		m_controlPackets.clear();
		m_readingBank = -1;
		m_filterUntil = _answered ? 0 : _nowMs + FilterTailMs;
	}

	void PresetsLink::editorSent(const std::vector<uint8_t>& _bytes, const uint64_t _nowMs)
	{
		m_editorRx.insert(m_editorRx.end(), _bytes.begin(), _bytes.end());
		forEachMessage(m_editorRx, [&](const std::vector<uint8_t>& _m)
		{
			// A greeting is a question and its answer, not an exchange to keep out of: Animatek NME
			// asks whether the synth is still there every so often, and a load waited for a silence
			// that did not come.
			if(isClavia(_m) && ccOf(_m) == CcIAm)
				return;
			m_lastActivity = _nowMs;
			// The editor talks: the link gets out of the way and reads the bank again once it is quiet.
			if(m_state == State::Listing)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_readWanted[static_cast<size_t>(m_bank)] = true;
				finish(_nowMs);
			}
			// The editor stores a patch (StorePatch: $41 $0B, slot, bank, position): the OS tells no
			// one, so a bank already read is read again once the editor is quiet, to show it.
			if(isClavia(_m) && ccOf(_m) == CcPatch && _m.size() >= 11 && _m[4] == 0x41 && _m[5] == 0x0b && _m[7] < Banks)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if(m_known[_m[7]])
					m_readWanted[_m[7]] = true;
			}
		});
	}

	void PresetsLink::g1Sent(const std::vector<uint8_t>& _bytes, const uint64_t _nowMs, std::vector<uint8_t>& _toEditor)
	{
		m_rx.insert(m_rx.end(), _bytes.begin(), _bytes.end());
		forEachMessage(m_rx, [&](const std::vector<uint8_t>& _m)
		{
			if(!isClavia(_m))
			{
				_toEditor.insert(_toEditor.end(), _m.begin(), _m.end());
				return;
			}
			const bool hide = hides(_m, _nowMs);	// before the reply moves the state on
			takeReply(_m, _nowMs);
			traceMsg(hide ? "<-" : "<= (to the editor)", _nowMs, _m);
			if(!hide)
				_toEditor.insert(_toEditor.end(), _m.begin(), _m.end());
		});
		// The link's own word to the editor, after what the G1 said: also when the G1 says nothing
		// (an upload loaded only, with no Store to answer after it).
		if(!m_toEditor.empty())
		{
			traceMsg("=> (to the editor, ours)", _nowMs, m_toEditor);
			_toEditor.insert(_toEditor.end(), m_toEditor.begin(), m_toEditor.end());
			m_toEditor.clear();
		}
	}

	// The ACKs to the link's own requests, while one is open or shortly after: not the editor's.
	// Only those of the kinds its requests get (a list, a load, an upload's packets), so an ACK
	// to the editor's own request is never kept: Animatek NME asks for the patch the moment it
	// hears of a load, and its answer, kept as the link's, made it wait seconds for a retry.
	bool PresetsLink::hides(const std::vector<uint8_t>& _m, const uint64_t _nowMs) const
	{
		if(!(m_state != State::Idle || _nowMs < m_filterUntil))
			return false;
		// The ControlMap's packets, for the slot the link asked about.
		if(m_hideState == State::ControlsRead && isPacket(ccOf(_m)) && slotOf(_m) == m_controlJob.slot)
			return true;
		if(ccOf(_m) != CcAck || _m.size() < 7)
			return false;
		const uint8_t type = _m[5];
		switch(m_hideState)
		{
		case State::Listing:
		case State::Storing:	return type == ListAckA || type == ListAckB;	// a store is answered with the list there
		case State::Loading:	return type == LoadAck;
		case State::Uploading:	return type == PacketAck || type == LastPacketAck;
		case State::ControlsPid:
		case State::ControlsAssign:
		case State::ControlsRead:	return type == PatchIdAck || type == LastPacketAck;	// the patch's id, and the assignment's ACK if any
		default:				return false;
		}
	}

	void PresetsLink::takeReply(const std::vector<uint8_t>& _m, const uint64_t _nowMs)
	{
		// The ControlMap, as one or more packets: once whole, it is what the slot's patch has.
		if(m_state == State::ControlsRead && isPacket(ccOf(_m)) && slotOf(_m) == m_controlJob.slot && _m.size() >= 7)
		{
			const auto cc = ccOf(_m);
			if(cc & 1)		// the first packet
				m_controlPackets.clear();
			m_controlPackets.insert(m_controlPackets.end(), _m.begin() + 5, _m.end() - 2);	// after cmd/pid, before checksum
			if(!(cc & 2))	// not the last one yet
				return;
			const auto list = decodeControls(unpack7(m_controlPackets));
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				auto& c = m_controls[static_cast<size_t>(m_controlJob.slot)];
				c.known = true;
				c.list = list;
				++c.revision;
			}
			// The OS does not tell the editor of an assignment made through the PC Port, which it
			// takes for the editor's own: the link tells it the patch changed, so it reads it again.
			// After the read, so the editor's reading does not run into the link's. Twice, as the OS
			// says it after a panel load: Animatek NME passes over one such message after its own
			// upload or edit, also when the OS never sent it, and would miss a single one.
			if(m_controlJob.assign)
			{
				const auto slot = static_cast<uint8_t>(m_controlJob.slot);
				const auto notice = frame(CcInfo, slot, {0x01, InfoNewPatchInSlot, slot, m_controlPid});
				m_toEditor = notice;
				m_toEditor.insert(m_toEditor.end(), notice.begin(), notice.end());
			}
			return finish(_nowMs, true);
		}
		if(ccOf(_m) != CcAck || _m.size() < 7)
			return;
		if(m_state == State::ControlsPid)
		{
			if(_m[5] == PatchIdAck && slotOf(_m) == m_controlJob.slot)
			{
				m_controlPid = _m[6] & 0x7f;
				m_controlNext = true;	// the assignment, or the read, at the next tick
			}
			return;
		}
		if(m_state == State::ControlsAssign)
		{
			if(_m[5] == LastPacketAck && slotOf(_m) == m_controlJob.slot)
				m_controlNext = true;	// taken: the read, at the next tick
			return;
		}
		if(m_state == State::Loading)
		{
			// The OS tells an editor of a patch loaded from the panel or by MIDI (NewPatchInSlot), but
			// not of one loaded through the PC Port, which it takes for the editor's own: the editor
			// would never fetch it. So the link tells it, as the OS would have: the slot, and the
			// patch's new id from this ACK (f0 33 50 06 01 38 00 01 33 f7 for slot A, id 1).
			if(_m[5] == LoadAck && _m.size() > 8)
			{
				const uint8_t slot = _m[6] & 3, pid = _m[7] & 0x7f;
				m_toEditor = frame(CcInfo, slot, {_m[4], InfoNewPatchInSlot, slot, pid});
			}
			return finish(_nowMs, true);
		}
		if(m_state == State::Uploading)
		{
			if(_m[5] != PacketAck && _m[5] != LastPacketAck)
				return;
			if(_m[5] == PacketAck)
				m_uploadPid = _m[6] & 0x7f;	// the patch's new id, as an editor's upload learns it
			++m_frame;
			m_uploadNext = true;		// the next packet, or the store, at the next tick
			return;
		}
		if(m_state == State::Storing)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_readWanted[static_cast<size_t>(m_upload.bank)] = true;	// the list shows it
			uploadDone(true, "Stored in bank " + std::to_string(m_upload.bank + 1) + " at " + std::to_string(m_upload.position + 1) + ".");
			finish(_nowMs, true);
			return;
		}
		if(_m.size() < 9)
			return;
		if(m_state != State::Listing || (_m[5] != ListAckA && _m[5] != ListAckB))
			return;

		// After pid, type and pid, before the checksum and F7.
		const std::vector<uint8_t> content(_m.begin() + 7, _m.end() - 2);
		int nextBank = -1, nextPosition = -1;
		const auto entries = decodeList(content, m_bank, m_position, nextBank, nextPosition);
		int furthest = m_position - 1;
		for(const auto& e : entries)
		{
			if(e.bank != m_bank || e.position < 0 || e.position >= Positions)
				continue;
			m_partial[static_cast<size_t>(e.position)] = e.name;
			furthest = std::max(furthest, e.position);
		}

		// Done with this bank when the list is over, moves to another bank, or reaches its end.
		int next = nextBank == m_bank ? nextPosition : -1;
		if(next >= 0 && next <= furthest)
			next = furthest + 1;
		if(next < 0 || next >= Positions || next <= m_position)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_names[static_cast<size_t>(m_bank)] = m_partial;
			m_known[static_cast<size_t>(m_bank)] = true;
			++m_revision;
			finish(_nowMs, true);
			return;
		}
		m_position = next;
		m_deadline = _nowMs + ReplyMs;
		m_pendingNext = true;
	}

	// Under m_mutex.
	void PresetsLink::uploadDone(const bool _ok, const std::string& _message)
	{
		m_uploadStatus.busy = !m_uploads.empty();
		m_uploadStatus.ok = _ok;
		m_uploadStatus.message = _message;
		++m_uploadStatus.serial;
		++m_revision;
	}

	void PresetsLink::tick(const uint64_t _nowMs, std::vector<uint8_t>& _toG1)
	{
		if(m_state == State::Uploading && m_uploadNext)
		{
			m_uploadNext = false;
			if(m_frame < m_upload.frames.size())
			{
				request(m_upload.frames[m_frame], State::Uploading, _nowMs, _toG1);
				m_deadline = _nowMs + PacketMs;
				return;
			}
			// All in: the editor is told of the new patch in the slot, as after a load (takeReply). Not
			// for a store of what the slot already had (storeSlot): no new patch there.
			const auto slot = static_cast<uint8_t>(m_upload.slot);
			if(!m_upload.frames.empty())
				m_toEditor = frame(CcInfo, slot, {0x01, InfoNewPatchInSlot, slot, m_uploadPid});
			if(m_upload.bank >= 0)
			{
				request(frame(CcPatch, static_cast<uint8_t>(m_upload.slot), {0x41, 0x0b, static_cast<uint8_t>(m_upload.slot),
					static_cast<uint8_t>(m_upload.bank), static_cast<uint8_t>(m_upload.position)}), State::Storing, _nowMs, _toG1);
				return;
			}
			std::lock_guard<std::mutex> lock(m_mutex);
			uploadDone(true, std::string("Loaded into slot ") + static_cast<char>('A' + m_upload.slot) + ".");
			finish(_nowMs, true);
			return;
		}
		// The patch's id came: the assignment goes, if there is one, and once the OS has taken it
		// (it ignores a request sent before its ACK), the ControlMap is read, to show what the
		// patch has now.
		if((m_state == State::ControlsPid || m_state == State::ControlsAssign) && m_controlNext)
		{
			m_controlNext = false;
			const auto slot = static_cast<uint8_t>(m_controlJob.slot);
			if(m_state == State::ControlsPid && m_controlJob.assign)
			{
				// As Animatek NME: a new CC ($22), or the previous one changed for it or removed ($23).
				std::vector<uint8_t> payload = {m_controlPid};
				if(m_controlJob.previous >= 0)
					payload.insert(payload.end(), {ChangeCtrl, static_cast<uint8_t>(m_controlJob.previous & 0x7f)});
				if(m_controlJob.ctrl >= 0)
					payload.insert(payload.end(), {AssignCtrl, static_cast<uint8_t>(m_controlJob.section & 0x7f), static_cast<uint8_t>(m_controlJob.module & 0x7f),
						static_cast<uint8_t>(m_controlJob.param & 0x7f), static_cast<uint8_t>(m_controlJob.ctrl & 0x7f)});
				request(frame(CcPatch, slot, payload), State::ControlsAssign, _nowMs, _toG1);
				return;
			}
			m_controlPackets.clear();
			request(frame(CcPatch, slot, {m_controlPid, GetControlMap}), State::ControlsRead, _nowMs, _toG1);
			return;
		}
		if(m_state == State::Listing && m_pendingNext)
		{
			m_pendingNext = false;
			request(frame(CcPatch, 0, {0x41, 0x14, static_cast<uint8_t>(m_bank), static_cast<uint8_t>(m_position)}), State::Listing, _nowMs, _toG1);
			return;
		}
		if(m_state != State::Idle)
		{
			if(_nowMs < m_deadline)
				return;
			// No answer: a list is tried again later, once the editor is quiet; an upload ends with
			// its empty last packet, so the OS is not left waiting for the rest, and says so.
			if(m_state == State::Listing)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_readWanted[static_cast<size_t>(m_bank)] = true;
			}
			else if(m_state == State::Uploading || m_state == State::Storing)
			{
				if(m_state == State::Uploading && !m_upload.abort.empty())
					_toG1.insert(_toG1.end(), m_upload.abort.begin(), m_upload.abort.end());
				std::lock_guard<std::mutex> lock(m_mutex);
				uploadDone(false, m_state == State::Uploading
					? "The synth did not take packet " + std::to_string(m_frame + 1) + " of " + std::to_string(m_upload.frames.size()) + "; nothing was stored."
					: std::string("The synth did not confirm the store."));
			}
			finish(_nowMs);
			return;
		}
		if(_nowMs < BootMs || _nowMs < m_filterUntil)
			return;

		// An upload or a load first: it is what the user is waiting for, and it waits less for
		// the editor to be quiet than a list does.
		std::lock_guard<std::mutex> lock(m_mutex);
		const bool userWaits = !m_uploads.empty() || !m_loads.empty() || !m_controlJobs.empty();
		if(_nowMs < m_lastActivity + (userWaits ? LoadQuietMs : QuietMs))
			return;
		if(!m_uploads.empty())
		{
			m_upload = std::move(m_uploads.front());
			m_uploads.erase(m_uploads.begin());
			m_frame = 0;
			// Nothing to send first (storeSlot): straight to the Store.
			if(m_upload.frames.empty())
			{
				request(frame(CcPatch, static_cast<uint8_t>(m_upload.slot), {0x41, 0x0b, static_cast<uint8_t>(m_upload.slot),
					static_cast<uint8_t>(m_upload.bank), static_cast<uint8_t>(m_upload.position)}), State::Storing, _nowMs, _toG1);
				m_deadline = _nowMs + PacketMs;
				return;
			}
			request(m_upload.frames[0], State::Uploading, _nowMs, _toG1);
			m_deadline = _nowMs + PacketMs;
			return;
		}
		if(!m_loads.empty())
		{
			const auto l = m_loads.front();
			m_loads.erase(m_loads.begin());
			request(frame(CcPatch, static_cast<uint8_t>(l.slot), {0x41, 0x0a, static_cast<uint8_t>(l.slot), static_cast<uint8_t>(l.bank), static_cast<uint8_t>(l.position)}),
				State::Loading, _nowMs, _toG1);
			return;
		}
		if(!m_controlJobs.empty())
		{
			m_controlJob = m_controlJobs.front();
			m_controlJobs.erase(m_controlJobs.begin());
			request(frame(CcPatch, static_cast<uint8_t>(m_controlJob.slot), {0x41, 0x35}), State::ControlsPid, _nowMs, _toG1);	// RequestPatch
			return;
		}
		for(int b = 0; b < Banks; ++b)
		{
			if(!m_readWanted[static_cast<size_t>(b)])
				continue;
			m_readWanted[static_cast<size_t>(b)] = false;
			m_bank = b;
			m_position = 0;
			m_partial = {};
			m_readingBank = b;
			request(frame(CcPatch, 0, {0x41, 0x14, static_cast<uint8_t>(b), 0}), State::Listing, _nowMs, _toG1);
			return;
		}
	}

	// _msg to the G1, and the link waits in _state. What the G1 answers is hidden from the editor
	// until the transaction ends, and a little after (finish).
	void PresetsLink::request(const std::vector<uint8_t>& _msg, const State _state, const uint64_t _nowMs, std::vector<uint8_t>& _toG1)
	{
		traceMsg("->", _nowMs, _msg);
		_toG1.insert(_toG1.end(), _msg.begin(), _msg.end());
		m_filterUntil = UINT64_MAX;
		m_state = _state;
		m_hideState = _state;		// what its late answers look like, after it is over
		m_deadline = _nowMs + ReplyMs;
	}
}
