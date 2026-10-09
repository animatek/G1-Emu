#include "slotkeeper.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace g1app
{
	namespace
	{
		// How long the PC Port must be quiet before the keeper asks for a slot: an editor's own
		// exchanges come in bursts much closer together than this, and the keeper never wants to
		// be in the middle of one.
		constexpr uint64_t QuietMs = 1000;
		constexpr uint64_t BootMs = 2500;			// the OS is up and has looked at its flash
		constexpr uint64_t ReplyMs = 1500;			// a GetPatch section, the RequestPatch ACK
		constexpr uint64_t UploadAckMs = 4000;		// a packet that makes the OS reload the DSPs is slow
		constexpr uint64_t FilterTailMs = 300;		// late replies to an abandoned request

		constexpr uint8_t CcIAm = 0x00, CcParameter = 0x13, CcInfo = 0x14, CcAck = 0x16, CcPatch = 0x17;
		constexpr uint8_t InfoNewPatchInSlot = 0x38;

		// The 13 GetPatch requests, as NME and nmedit send them (sc, then a payload byte or none).
		struct Request { uint8_t sc; int payload; };
		constexpr Request g_requests[] = {
			{0x20, 0x28}, {0x4b, 1}, {0x4b, 0}, {0x53, 1}, {0x53, 0}, {0x4c, 1}, {0x4c, 0},
			{0x66, -1}, {0x63, -1}, {0x61, -1}, {0x4e, 1}, {0x4e, 0}, {0x68, -1}};
		constexpr size_t RequestCount = sizeof(g_requests) / sizeof(g_requests[0]);

		// Their replies, by index: 0 Header+PatchName2, 1-2 modules, 3-4 cables, 5-6 parameters,
		// 7 MorphMap, 8 KnobMap, 9 ControlMap, 10-11 NameDump, 12 NoteDump.
		constexpr uint8_t g_replyTypes[RequestCount] = {33, 74, 74, 82, 82, 77, 77, 101, 98, 96, 90, 90, 105};
		constexpr size_t HeaderBytes = 11;			// type (8 bits) + Header (80 bits): no padding
		constexpr uint8_t SettingsSection = 3;		// the synth settings' section (synthsettings.h)

		bool isGetPatch(const uint8_t _sc)
		{
			for(const auto& r : g_requests)
				if(r.sc == _sc)
					return true;
			return false;
		}

		std::vector<uint8_t> frame(const uint8_t _cc, const uint8_t _slot, const std::vector<uint8_t>& _payload)
		{
			std::vector<uint8_t> m = {0xf0, 0x33, static_cast<uint8_t>(((_cc & 0x1f) << 2) | (_slot & 3)), 0x06};
			m.insert(m.end(), _payload.begin(), _payload.end());
			uint32_t sum = 0;
			for(const auto b : m)
				sum += b;
			m.push_back(static_cast<uint8_t>(sum & 0x7f));
			m.push_back(0xf7);
			return m;
		}

		std::vector<uint8_t> pack7(const std::vector<uint8_t>& _raw)
		{
			std::vector<uint8_t> out;
			uint32_t buffer = 0;
			int held = 0;
			for(const auto b : _raw)
			{
				buffer = (buffer << 8) | b;
				held += 8;
				while(held >= 7)
				{
					held -= 7;
					out.push_back(static_cast<uint8_t>((buffer >> held) & 0x7f));
				}
			}
			if(held > 0)
				out.push_back(static_cast<uint8_t>((buffer << (7 - held)) & 0x7f));
			return out;
		}

		std::vector<uint8_t> unpack7(const std::vector<uint8_t>& _in)
		{
			std::vector<uint8_t> out;
			uint32_t buffer = 0;
			int held = 0;
			for(const auto c : _in)
			{
				buffer = (buffer << 7) | (c & 0x7f);
				held += 7;
				if(held >= 8)
				{
					held -= 8;
					out.push_back(static_cast<uint8_t>((buffer >> held) & 0xff));
				}
			}
			return out;
		}

		// Whole SysEx messages out of a byte stream; what is left over stays in _buf.
		template<typename F> void forEachMessage(std::vector<uint8_t>& _buf, F&& _f)
		{
			size_t pos = 0;
			for(;;)
			{
				const auto start = std::find(_buf.begin() + static_cast<long>(pos), _buf.end(), uint8_t(0xf0));
				if(start == _buf.end())
				{
					pos = _buf.size();
					break;
				}
				const auto end = std::find(start, _buf.end(), uint8_t(0xf7));
				if(end == _buf.end())
				{
					pos = static_cast<size_t>(start - _buf.begin());
					break;
				}
				_f(std::vector<uint8_t>(start, end + 1));
				pos = static_cast<size_t>(end - _buf.begin()) + 1;
			}
			_buf.erase(_buf.begin(), _buf.begin() + static_cast<long>(pos));
		}

		// G1_KEEPER_TRACE=1: every message the keeper sends and every one the G1 sends, on stderr.
		bool trace()
		{
			static const bool on = std::getenv("G1_KEEPER_TRACE") != nullptr;
			return on;
		}

		void traceMsg(const char* _dir, const uint64_t _nowMs, const std::vector<uint8_t>& _m)
		{
			if(!trace())
				return;
			std::fprintf(stderr, "[keeper %6llu ms] %s", static_cast<unsigned long long>(_nowMs), _dir);
			for(size_t i = 0; i < _m.size() && i < 24; ++i)
				std::fprintf(stderr, " %02x", _m[i]);
			std::fprintf(stderr, _m.size() > 24 ? " ...\n" : "\n");
		}

		bool isClavia(const std::vector<uint8_t>& _m) { return _m.size() >= 6 && _m[1] == 0x33; }
		uint8_t ccOf(const std::vector<uint8_t>& _m) { return static_cast<uint8_t>(_m[2] >> 2); }
		uint8_t slotOf(const std::vector<uint8_t>& _m) { return static_cast<uint8_t>(_m[2] & 3); }
	}

	// ____________________________________________________________________________________________
	// Sections

	bool SlotKeeper::repliesToSections(const std::vector<std::vector<uint8_t>>& _replies, std::vector<std::vector<uint8_t>>& _sections)
	{
		if(_replies.size() != RequestCount)
			return false;
		for(size_t i = 0; i < RequestCount; ++i)
			if(_replies[i].empty() || _replies[i][0] != g_replyTypes[i])
				return false;
		// The header reply is the Header and then PatchName2 (39), each on whole bytes. An upload
		// starts with PatchName (55), which is the same name after three zero bytes.
		const auto& h = _replies[0];
		if(h.size() < HeaderBytes + 2 || h[HeaderBytes] != 39)
			return false;
		std::vector<uint8_t> name = {55, 0, 0, 0};
		name.insert(name.end(), h.begin() + HeaderBytes + 1, h.end());

		// NME's upload order. The OS gives no CustomDump back: what it carries in a .pch is the
		// editor's own (how a frequency is shown, a zoom, a slider), so an empty one goes up.
		_sections.clear();
		_sections.push_back(std::move(name));
		_sections.emplace_back(h.begin(), h.begin() + HeaderBytes);
		for(const size_t i : {1, 2, 12, 3, 4, 5, 6, 7, 8, 9})
			_sections.push_back(_replies[i]);
		_sections.push_back({91, 0x80});	// CustomDump, poly, no modules
		_sections.push_back({91, 0x00});	// CustomDump, common, no modules
		_sections.push_back(_replies[10]);
		_sections.push_back(_replies[11]);
		return true;
	}

	std::vector<std::vector<uint8_t>> SlotKeeper::uploadMessages(const std::vector<std::vector<uint8_t>>& _sections, const uint8_t _slot)
	{
		// As NME's UploadPacketizer: the sections back to back, cut into packets of 32 bytes; each
		// packet says how many sections end in it. cc $1C, +1 on the first, +2 on the last.
		constexpr size_t PacketBytes = 32;
		struct Packet { std::vector<uint8_t> data; uint8_t ended = 0; };
		std::vector<Packet> packets(1);
		for(const auto& s : _sections)
		{
			for(const auto b : s)
			{
				if(packets.back().data.size() == PacketBytes)
					packets.emplace_back();
				packets.back().data.push_back(b);
			}
			++packets.back().ended;
		}
		std::vector<std::vector<uint8_t>> out;
		for(size_t i = 0; i < packets.size(); ++i)
		{
			const auto cc = static_cast<uint8_t>(0x1c | (i == 0 ? 1 : 0) | (i + 1 == packets.size() ? 2 : 0));
			std::vector<uint8_t> payload = {static_cast<uint8_t>(0x40 | (packets[i].ended & 0x3f))};
			const auto bits = pack7(packets[i].data);
			payload.insert(payload.end(), bits.begin(), bits.end());
			out.push_back(frame(cc, _slot, payload));
		}
		return out;
	}

	// ____________________________________________________________________________________________
	// Project state: "G1SK" 1, then per slot a section count and each section's length and bytes.

	std::vector<uint8_t> SlotKeeper::pack(const Slots& _slots)
	{
		std::vector<uint8_t> out = {'G', '1', 'S', 'K', 1};
		auto u16 = [&out](const size_t _v) { out.push_back(static_cast<uint8_t>(_v >> 8)); out.push_back(static_cast<uint8_t>(_v)); };
		for(const auto& s : _slots)
		{
			out.push_back(static_cast<uint8_t>(s.sections.size()));
			for(const auto& sec : s.sections)
			{
				u16(sec.size());
				out.insert(out.end(), sec.begin(), sec.end());
			}
		}
		return out;
	}

	bool SlotKeeper::unpack(const std::vector<uint8_t>& _bytes, Slots& _slots)
	{
		if(_bytes.size() < 5 || _bytes[0] != 'G' || _bytes[1] != '1' || _bytes[2] != 'S' || _bytes[3] != 'K' || _bytes[4] != 1)
			return false;
		Slots slots;
		size_t p = 5;
		for(auto& s : slots)
		{
			if(p >= _bytes.size())
				return false;
			const size_t n = _bytes[p++];
			for(size_t i = 0; i < n; ++i)
			{
				if(p + 2 > _bytes.size())
					return false;
				const size_t len = (static_cast<size_t>(_bytes[p]) << 8) | _bytes[p + 1];
				p += 2;
				if(p + len > _bytes.size())
					return false;
				s.sections.emplace_back(_bytes.begin() + static_cast<long>(p), _bytes.begin() + static_cast<long>(p + len));
				p += len;
			}
		}
		if(p != _bytes.size())
			return false;
		_slots = std::move(slots);
		return true;
	}

	// ____________________________________________________________________________________________
	// Any thread

	SlotKeeper::Slots SlotKeeper::slots() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_slots;
	}

	void SlotKeeper::restore(const Slots& _slots)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_pendingRestore = _slots;
		m_hasRestore = true;
	}

	void SlotKeeper::reread(const size_t _slot)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(_slot < SlotCount)
			m_dirty[_slot] = true;
	}

	bool SlotKeeper::settled() const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(m_hasRestore || m_state != State::Idle)
			return false;
		// Restored slots still to be sent count too: between two uploads the keeper is idle.
		const auto any = [](const auto& _flags) { return std::any_of(_flags.begin(), _flags.end(), [](const bool _f) { return _f; }); };
		return !any(m_dirty) && !any(m_uploadPending);
	}

	// ____________________________________________________________________________________________
	// Worker thread

	void SlotKeeper::send(std::vector<uint8_t> _msg, const uint64_t _nowMs, std::vector<uint8_t>& _toG1)
	{
		traceMsg("->", _nowMs, _msg);
		_toG1.insert(_toG1.end(), _msg.begin(), _msg.end());
		m_filterUntil = UINT64_MAX;	// until the transaction ends, then a tail
	}

	void SlotKeeper::abort(const uint64_t _nowMs)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(m_state == State::Requesting || m_state == State::Fetching)
			m_dirty[m_slot] = true;		// asked again once things are quiet
		m_state = State::Idle;
		m_filterUntil = _nowMs + FilterTailMs;
		m_replies.clear();
		m_section.clear();
	}

	void SlotKeeper::editorSent(const std::vector<uint8_t>& _bytes, const uint64_t _nowMs)
	{
		m_editorRx.insert(m_editorRx.end(), _bytes.begin(), _bytes.end());
		forEachMessage(m_editorRx, [&](const std::vector<uint8_t>& _m)
		{
			traceMsg("ed", _nowMs, _m);
			// A greeting is a question and its answer, not an exchange to keep out of: Animatek NME
			// asks whether the synth is still there every so often (PresetsLink::editorSent).
			if(isClavia(_m) && ccOf(_m) == CcIAm)
				return;
			m_lastActivity = _nowMs;
			if(m_state == State::Requesting || m_state == State::Fetching)
				abort(_nowMs);		// the editor talks: the keeper gets out of the way
			if(!isClavia(_m))
				return;
			const auto cc = ccOf(_m);
			// An upload's packets must follow each other with nothing in between: from its first
			// packet to its last the keeper keeps quiet, however slow the editor is.
			// No time limit: while a packet waits for the next one the DSPs are stopped, and with no
			// audio to pace it the plugin's G1 runs far ahead of the wall clock. Anything else the
			// editor says ends the wait.
			m_editorUploading = cc >= 0x1c && cc <= 0x1f && !(cc & 2);
			bool changes = cc == CcParameter || (cc >= 0x1c && cc <= 0x1f);
			uint8_t slot = slotOf(_m);
			if(cc == CcPatch)
			{
				if(_m[4] == 0x41)	// PatchManagerCommand: only LoadPatch (bank to slot) changes a slot
				{
					changes = _m.size() > 7 && _m[5] == 0x0a;
					if(changes)
						slot = static_cast<uint8_t>(_m[6] & 3);
				}
				else if(_m[4] == 0x44)
					changes = false;	// the synth settings (RequestSynthSettings): no slot's patch
				else
					changes = !isGetPatch(_m[5]);	// a patch modification, not a read
			}
			// Nor is the synth settings section (type 3) sent back in one packet, as an editor (or
			// the SynthSettingsLink) writes them: it would have the keeper read slot A for nothing.
			if(cc == 0x1f && _m.size() > 8)
			{
				const auto raw = unpack7(std::vector<uint8_t>(_m.begin() + 5, _m.end() - 2));
				if(!raw.empty() && raw[0] == SettingsSection)
					changes = false;
			}
			if(changes)
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				m_dirty[slot] = true;
			}
		});
	}

	void SlotKeeper::g1Sent(const std::vector<uint8_t>& _bytes, const uint64_t _nowMs, std::vector<uint8_t>& _toEditor)
	{
		m_rx.insert(m_rx.end(), _bytes.begin(), _bytes.end());
		forEachMessage(m_rx, [&](const std::vector<uint8_t>& _m)
		{
			traceMsg("<-", _nowMs, _m);
			const bool mine = m_state != State::Idle && m_state != State::Boot;
			const bool filtering = mine || _nowMs < m_filterUntil;
			bool hide = false;
			if(isClavia(_m))
			{
				const auto cc = ccOf(_m);
				const auto slot = slotOf(_m);
				// What the keeper's own requests bring: ACKs, patch packets and the IAm answer.
				// The IAm answer only while the keeper greets: the editor greets too, and must hear back.
				if(filtering && ((cc == CcAck && slot == m_slot) || (cc >= 0x1c && cc <= 0x1f && slot == m_slot)))
					hide = true;
				if(cc == CcIAm && m_state == State::Greeting)
					hide = true;
				// The G1 reporting a change of its own: a panel knob or MIDI moved a parameter, a
				// patch came into a slot from the panel or a Program Change. A panel knob comes as an
				// Info message ($14 $01 $40, section, module, parameter, value), not as a Parameter.
				if((cc == CcParameter && _m[4] == 0x40) || (cc == CcInfo && _m.size() > 6 && _m[4] == 0x01 && _m[5] == 0x40))
				{
					std::lock_guard<std::mutex> lock(m_mutex);
					m_dirty[slot] = true;
					m_lastActivity = _nowMs;
				}
				else if(cc == CcInfo && _m.size() > 7 && _m[5] == InfoNewPatchInSlot)
				{
					const auto target = static_cast<size_t>(_m[6] & 3);
					// Not the slot the keeper is filling right now: that one it knows.
					if(!(m_state == State::Uploading && target == m_slot))
					{
						std::lock_guard<std::mutex> lock(m_mutex);
						m_dirty[target] = true;
						m_lastActivity = _nowMs;
					}
				}
				if(mine)
					handle(_m, _nowMs);
			}
			if(!hide)
				_toEditor.insert(_toEditor.end(), _m.begin(), _m.end());
		});
	}

	void SlotKeeper::handle(const std::vector<uint8_t>& _m, const uint64_t _nowMs)
	{
		const auto cc = ccOf(_m);
		const auto slot = slotOf(_m);
		switch(m_state)
		{
		case State::Greeting:
			if(cc == CcIAm && _m.size() > 4 && _m[4] == 0x01)
			{
				m_state = State::Idle;
				m_filterUntil = _nowMs + FilterTailMs;
			}
			break;
		case State::Requesting:
			if(cc == CcAck && slot == m_slot && _m.size() > 6 && _m[5] == 0x36)
			{
				m_pid = _m[6];
				m_state = State::Fetching;
				m_step = 0;
				m_replies.clear();
				m_section.clear();
				m_deadline = 0;		// tick() sends the first section request
			}
			break;
		case State::Fetching:
			if(cc >= 0x1c && cc <= 0x1f && slot == m_slot && _m.size() >= 7)
			{
				if(cc & 1)
					m_section.clear();
				m_section.insert(m_section.end(), _m.begin() + 5, _m.end() - 2);	// after cmd/pid, before checksum
				if(cc & 2)
				{
					m_replies.push_back(unpack7(m_section));
					m_section.clear();
					++m_step;
					m_deadline = 0;	// tick() sends the next request, or finishes
				}
			}
			break;
		case State::Uploading:
			if(cc == CcAck && slot == m_slot && _m.size() > 5 && (_m[5] == 0x36 || _m[5] == 0x7f))
			{
				++m_step;
				m_deadline = 0;
			}
			break;
		default:
			break;
		}
	}

	void SlotKeeper::finishFetch()
	{
		std::vector<std::vector<uint8_t>> sections;
		const bool ok = repliesToSections(m_replies, sections);
		// An empty slot answers too, with a patch of no modules: kept as empty, so a restore
		// leaves it alone instead of uploading nothing into it.
		const auto noModules = [](const std::vector<uint8_t>& _dump) { return _dump.size() >= 2 && (_dump[1] & 0x7f) == 0; };
		if(ok && noModules(m_replies[1]) && noModules(m_replies[2]))
			sections.clear();
		std::lock_guard<std::mutex> lock(m_mutex);
		if(ok)
			m_slots[m_slot].sections = std::move(sections);
		else
			m_dirty[m_slot] = true;
		m_replies.clear();
	}

	void SlotKeeper::tick(const uint64_t _nowMs, std::vector<uint8_t>& _toG1)
	{
		auto endTransaction = [&]
		{
			m_state = State::Idle;
			m_filterUntil = _nowMs + FilterTailMs;
		};

		switch(m_state)
		{
		case State::Boot:
			if(_nowMs >= BootMs)
			{
				send({0xf0, 0x33, 0x00, 0x06, 0x00, 0x03, 0x03, 0xf7}, _nowMs, _toG1);	// IAm, as an editor
				m_state = State::Greeting;
				m_deadline = _nowMs + ReplyMs;
			}
			break;

		case State::Greeting:
			if(_nowMs >= m_deadline)
			{
				m_state = State::Boot;		// not up yet: greet again a little later
				m_filterUntil = _nowMs + FilterTailMs;
			}
			break;

		case State::Idle:
		{
			if(_nowMs < m_filterUntil)
				break;
			// A restore first: the slots as the project had them.
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				if(m_hasRestore)
				{
					m_slots = m_pendingRestore;
					m_hasRestore = false;
					for(size_t s = 0; s < SlotCount; ++s)
					{
						m_uploadPending[s] = !m_slots[s].empty();
						m_dirty[s] = m_slots[s].empty();	// nothing saved for it: learn what it has
					}
				}
			}
			for(size_t s = 0; s < SlotCount; ++s)
				if(m_uploadPending[s])
				{
					{
						// Together, so settled() never sees the slot neither waiting nor uploading.
						std::lock_guard<std::mutex> lock(m_mutex);
						m_uploadPending[s] = false;
						m_state = State::Uploading;
					}
					m_slot = s;
					m_upload = uploadMessages(m_slots[s].sections, static_cast<uint8_t>(s));
					m_step = 0;
					m_deadline = 0;
					return tick(_nowMs, _toG1);
				}
			if(_nowMs < m_lastActivity + QuietMs || m_editorUploading)
				break;
			size_t slot = SlotCount;
			{
				std::lock_guard<std::mutex> lock(m_mutex);
				for(size_t s = 0; s < SlotCount && slot == SlotCount; ++s)
					if(m_dirty[s])
						slot = s;
				if(slot != SlotCount)
					m_dirty[slot] = false;
			}
			if(slot == SlotCount)
				break;
			m_slot = slot;
			send(frame(CcPatch, static_cast<uint8_t>(slot), {0x41, 0x35}), _nowMs, _toG1);	// RequestPatch
			m_state = State::Requesting;
			m_deadline = _nowMs + ReplyMs;
			break;
		}

		case State::Requesting:
			if(_nowMs >= m_deadline)
			{
				// No patch to give: an empty slot (or one the OS will not talk about).
				std::lock_guard<std::mutex> lock(m_mutex);
				m_slots[m_slot].sections.clear();
				m_state = State::Idle;
				m_filterUntil = _nowMs + FilterTailMs;
			}
			break;

		case State::Fetching:
			if(m_deadline == 0)
			{
				if(m_step == RequestCount)
				{
					finishFetch();
					endTransaction();
					break;
				}
				const auto& r = g_requests[m_step];
				std::vector<uint8_t> payload = {m_pid, r.sc};
				if(r.payload >= 0)
					payload.push_back(static_cast<uint8_t>(r.payload));
				send(frame(CcPatch, static_cast<uint8_t>(m_slot), payload), _nowMs, _toG1);
				m_deadline = _nowMs + ReplyMs;
			}
			else if(_nowMs >= m_deadline)
				abort(_nowMs);
			break;

		case State::Uploading:
			if(m_deadline == 0)
			{
				if(m_step == m_upload.size())
				{
					m_upload.clear();
					endTransaction();
					break;
				}
				send(m_upload[m_step], _nowMs, _toG1);
				m_deadline = _nowMs + UploadAckMs;
			}
			else if(_nowMs >= m_deadline)
			{
				// The OS did not take it: learn what the slot has instead.
				m_upload.clear();
				std::lock_guard<std::mutex> lock(m_mutex);
				m_dirty[m_slot] = true;
				m_state = State::Idle;
				m_filterUntil = _nowMs + FilterTailMs;
			}
			break;
		}
	}
}
