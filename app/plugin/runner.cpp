#include "runner.h"

#include "diaglog.h"
#include "hostlog.h"

#include "g1Lib/g1thread.h"

#include <algorithm>
#include <string>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace g1app
{
	namespace
	{
		// How far past its target the worker can end up: the audio arrives a DSP sync at a time
		// (some 5 samples at 96 kHz), and the loop only looks between syncs. 0.5 ms is plenty.
		size_t guardFor(const double _rate)
		{
			return std::max<size_t>(32, static_cast<size_t>(std::lround(_rate * 0.0005)));
		}

		// How far ahead the worker may run: a whole block, plus a margin for its own unevenness.
		size_t aheadFor(const double _rate, const size_t _maxBlock)
		{
			double marginMs = 5.0;
			if(const char* v = std::getenv("G1_PLUGIN_MARGIN_MS"))
				marginMs = std::max(0.0, std::atof(v));
			return std::max<size_t>(_maxBlock, 32) + static_cast<size_t>(std::lround(_rate * marginMs / 1000.0));
		}
	}

	Runner::Runner(Engine& _engine, const double _rate, const size_t _maxBlock, const float _gainDb,
		MidiTransport* _pcPort, const int _pcIndex, SlotKeeper* _keeper, SynthSettingsLink* _settings, DirectLink* _link, PresetsLink* _presets)
		: m_engine(_engine), m_pcPort(_pcPort), m_link(_link), m_pcIndex(_pcIndex), m_keeper(_keeper), m_settings(_settings), m_presets(_presets), m_rate(_rate > 0 ? _rate : 48000.0), m_ahead(aheadFor(m_rate, _maxBlock)),
		  m_latency(m_ahead + guardFor(m_rate)),
		  m_bridge(std::pow(10.0f, _gainDb / 20.0f))
	{
		m_bridge.setRate(static_cast<uint32_t>(std::lround(m_rate)));

		auto& mc = m_engine.mc();
		// The outputs come out of the last DSP: 3 on a Regular, 7 on an Expanded.
		// The Micro has the one stereo output: what its OS puts on slots 3 and 4 is silence for the host (issue #83)
		const bool micro = mc.dspCount() == g1::g_microDsps;
		mc.outputDsp().setBlockCallback([this, micro](const int32_t _o1, const int32_t _o2, const int32_t _o3, const int32_t _o4)
		{
			constexpr auto dc = Engine::OutputDc;
			m_bridge.push(_o1 - dc, _o2 - dc, micro ? 0 : _o3 - dc, micro ? 0 : _o4 - dc);
			m_health.output(_o1 - dc, _o2 - dc);
		});
		mc.getDsp(0).setInputProvider([this](int32_t& _l, int32_t& _r) { m_bridge.pullInput(_l, _r); });

		char buf[160];
		std::snprintf(buf, sizeof(buf), "plugin at %.0f Hz, latency %zu frames (%.1f ms), %+.0f dB",
			m_rate, m_latency, 1000.0 * static_cast<double>(m_latency) / m_rate, static_cast<double>(_gainDb));
		m_stats.audio = buf;
		m_stats.midi = "the DAW track";
		g1app::logEvent(std::string("runner start: ") + buf + "\n  " + systemSummary(mc.threaded()));

		m_thread = std::thread([this] { run(); });
	}

	Runner::~Runner()
	{
		m_quit = true;
		m_thread.join();
		// The DSPs may still be finishing their last catch-up, which ends in these callbacks.
		auto& mc = m_engine.mc();
		mc.syncDsps();
		mc.outputDsp().setBlockCallback({});
		mc.getDsp(0).setInputProvider({});
		mc.setIdle(false);
	}

	void Runner::setGainDb(const float _gainDb)
	{
		m_bridge.setGain(std::pow(10.0f, _gainDb / 20.0f));
	}

	void Runner::queueMidi(const uint32_t _offset, const uint8_t* _data, const size_t _size)
	{
		if(_size == 0 || _size > 3 || _data[0] == 0xf0)
			return;
		Event e;
		e.frame = m_consumed.load(std::memory_order_relaxed) + m_latency + _offset;
		e.size = static_cast<uint8_t>(_size);
		std::copy_n(_data, _size, e.bytes);
		if(!m_events.push(e))
			++m_dropped;
	}

	void Runner::process(float* const* _outs, const size_t _numOuts, const float* const* _ins, const size_t _numIns, const size_t _frames, const bool _offline)
	{
		if(m_raceEnded.exchange(false))
		{
			m_bridge.skip(m_discard.exchange(0));	// the raced frames: nobody waited for them
			// Nor for those owed: a race slower than real time (the Expanded on a slower machine)
			// leaves a debt that would eat what comes after it, the skew lining the worker up with
			// what is queued, not with what is owed.
			m_debt = 0;
		}
		size_t done = 0;
		while(done < _frames)
		{
			// Never more than the emulator is allowed to be ahead, or it could never be there.
			const size_t n = std::min(_frames - done, m_ahead);
			if(_ins && _numIns)
				m_bridge.pushInputs(_ins, _numIns, done, n);

			// Offline there is no deadline: wait for the worker, which is already allowed to be
			// at least this far. Bounded all the same, in case it has died.
			if(_offline)
			{
				const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(10);
				while(m_bridge.queued() < m_debt + n && std::chrono::steady_clock::now() < giveUp)
					g1::preciseSleep(std::chrono::microseconds(50));
			}

			// A debt the worker cannot pay is given up: the G1 slower than real time (an Expanded on a
			// machine that cannot keep up) would otherwise owe more with every block, every frame it
			// made would be dropped as late, and it went silent for good instead of breaking up. Beyond
			// what it may be ahead by, the worker's timeline moves back by the debt instead (skew).
			if(m_debt > m_ahead)
			{
				m_forgiven.fetch_add(m_debt, std::memory_order_relaxed);
				m_debt = 0;
			}
			if(m_debt)
				m_debt -= m_bridge.skip(m_debt);
			const size_t got = m_debt ? 0 : m_bridge.pull(_outs, _numOuts, done, n);
			if(got < n)
			{
				for(size_t c = 0; c < _numOuts; ++c)
					if(_outs[c])
						std::fill(_outs[c] + done + got, _outs[c] + done + n, 0.0f);
				m_debt += n - got;
				if(m_primed)
					++m_xruns;
			}
			if(got)
				m_primed = true;

			// Only now may the worker go further, so the events queued for this block are all in.
			m_consumed.store(m_consumed.load(std::memory_order_relaxed) + n, std::memory_order_release);
			done += n;
		}
		for(size_t c = AudioBridge::Outs; c < _numOuts; ++c)
			if(_outs[c])
				std::fill(_outs[c], _outs[c] + _frames, 0.0f);
	}

	void Runner::run()
	{
		auto& mc = m_engine.mc();
		g1::keepThreadAtFullSpeed();
		using clock = std::chrono::steady_clock;
		auto lastStats = clock::now();
		uint64_t lastCycles = mc.ucCycles();
		double busy = 0;
		std::vector<uint8_t> out, pcOut, toEditor, toG1, linkIn;
		std::vector<std::vector<uint8_t>> pcIn;
		auto nowMs = [&mc] { return mc.ucCycles() / (g1::g_ucClock / 1000); };
		// The keeper's, the settings' and the presets' own requests, when the editor leaves them
		// room. Each takes the others' for an editor's, so they never talk over each other.
		const auto speak = [&](auto* _link, auto*... _others)
		{
			if(!_link)
				return;
			toG1.clear();
			_link->tick(nowMs(), toG1);
			if(toG1.empty())
				return;
			mc.getPcPort().receive(toG1);
			const auto tell = [&](auto* _other) { if(_other) _other->editorSent(toG1, nowMs()); };
			(tell(_others), ...);
		};
		// Each sees all the G1 sends, and keeps the answers to its own requests from the editor.
		const auto keepOwn = [&](auto* _link)
		{
			if(!_link)
				return;
			toEditor.clear();
			_link->g1Sent(pcOut, nowMs(), toEditor);
			pcOut.swap(toEditor);
		};

		// The boot and a project's restore race: the G1 runs as fast as it goes, ahead of the host,
		// until the OS answers the settings link and while the restore holds every note back (no
		// one can hear it). Then the frames it got ahead by are skipped (skew), those still queued
		// dropped, and the pacing goes on from there. G1_FAST_BOOT=0 keeps real time throughout.
		// Without a settings link (the tests) nothing says when the OS is up: no race.
		const bool fastBoot = [] { const char* v = std::getenv("G1_FAST_BOOT"); return !(v && *v == '0'); }();
		bool osUp = !m_settings, raced = false;
		uint64_t skew = 0;

		while(!m_quit.load(std::memory_order_acquire))
		{
			const auto t0 = clock::now();
			const auto* restoreFlag = m_restoreFlag.load(std::memory_order_relaxed);
			const bool restoring = restoreFlag && restoreFlag->load(std::memory_order_relaxed);
			const bool racing = fastBoot && (!osUp || restoring);
			if(raced && !racing)
			{
				// From here the host's frames line up with the G1's again: of what is queued, the boot's
				// and the restore's, all but what the worker may be ahead by is dropped, and the frames
				// raced ahead are skipped.
				raced = false;
				const auto queued = m_bridge.queued();
				m_forgiven.store(0);	// the new skew takes in whatever was owed
				m_discard.store(queued > m_ahead ? queued - m_ahead : 0);
				// Wraps, as it is meant to, when the race was slower than real time.
				skew = m_bridge.produced() - m_consumed.load(std::memory_order_acquire) - std::min<uint64_t>(queued, m_ahead);
				m_raceEnded.store(true);
			}
			raced |= racing;
			skew -= m_forgiven.exchange(0, std::memory_order_relaxed);	// wraps too, as it is meant to
			const uint64_t target = racing ? UINT64_MAX : m_consumed.load(std::memory_order_acquire) + m_ahead + skew;
			uint64_t produced = m_bridge.produced();

			// The events whose frame has come. An event is queued for consumed + latency + offset,
			// and the worker never gets past consumed + ahead plus one sync, which is less: so no
			// event is ever behind the emulator, and each one reaches the G1 at the first sync at
			// or after its frame, wherever the host's blocks begin and end.
			std::unique_lock<std::mutex> lock(m_engineMutex);
			// The editor's bytes, whenever they come: the PC Port has no timing to keep. But not while
			// the keeper uploads a slot (a project's slots going back in, as NME connects): a message
			// between two of its packets makes the OS drop the upload half done, and the slot shows
			// Error (#75). They wait, in order, until the upload ends.
			if(m_pcPort)
			{
				m_pcPort->poll(pcIn);
				if(static_cast<size_t>(m_pcIndex) < pcIn.size() && !pcIn[static_cast<size_t>(m_pcIndex)].empty())
				{
					auto& bytes = pcIn[static_cast<size_t>(m_pcIndex)];
					m_pcIn += bytes.size();
					m_editorHeld.insert(m_editorHeld.end(), bytes.begin(), bytes.end());
					bytes.clear();
				}
			}
			// The editor on the direct link (NME, with no MIDI port: the way in on Windows) talks to the
			// same PC Port.
			if(m_link)
			{
				linkIn.clear();
				m_link->poll(linkIn);
				m_pcIn += linkIn.size();
				m_editorHeld.insert(m_editorHeld.end(), linkIn.begin(), linkIn.end());
			}
			if(!m_editorHeld.empty() && !(m_keeper && m_keeper->uploading()))
			{
				mc.getPcPort().receive(m_editorHeld);
				if(m_keeper)
					m_keeper->editorSent(m_editorHeld, nowMs());
				if(m_settings)
					m_settings->editorSent(m_editorHeld, nowMs());
				if(m_presets)
					m_presets->editorSent(m_editorHeld, nowMs());
				m_editorHeld.clear();
			}
			speak(m_keeper, m_settings, m_presets);
			if(!(m_keeper && m_keeper->editorUploading(nowMs())))
			{
				speak(m_settings, m_keeper, m_presets);
				speak(m_presets, m_keeper, m_settings);
			}
			bool midiIn = false;
			while(const auto* e = m_events.front())
			{
				if(e->frame + skew > produced)
					break;
				// Each note moved by its slot's octave shift, which the OS keeps but does not apply
				// (with Octave Shift On).
				m_routed.clear();
				m_midiRouting.apply(mc, MidiRouting::layout(m_settings, m_selectedControllers, m_octaveShift), e->bytes, e->size, m_routed);
				for(const auto b : m_routed)
					mc.getSci().write(b);
				m_midiIn += e->size;
				// A note (on, with a velocity) is what should make a sound: the clock does not count.
				midiIn |= (e->bytes[0] & 0xf0) == 0x90 && e->size == 3 && e->bytes[2] != 0;
				m_events.pop();
			}
			if(midiIn)
				m_health.midiIn(mc, nowMs());
			m_health.tick(mc, nowMs());

			if(produced >= target)
			{
				lock.unlock();
				mc.setIdle(true);	// the DSP threads sleep too instead of spinning
				g1::preciseSleep(std::chrono::microseconds(200));	// sleep_for: 15.6 ms on Windows
				mc.setIdle(false);
			}
			else
			{
				uint64_t limit = target;
				if(const auto* e = m_events.front())
					limit = std::min(limit, e->frame + skew);
				// A slice of at most 0.5 ms of G1 time, so the lock is let go often.
				const auto stop = mc.ucCycles() + g1::g_ucClock / 2000;
				while(produced < limit && mc.ucCycles() < stop)
				{
					mc.exec();
					produced = m_bridge.produced();
				}
				// The G1's MIDI OUT goes to the host as the plugin's MIDI output, at the host frame its
				// sound plays at (produced less the skew); its PC Port goes to the editor, if there is
				// one. Not while racing: those frames are skipped, and no one hears them.
				out.clear();
				mc.getSci().read(out);
				m_midiOut += out.size();
				for(const auto b : out)
				{
					MidiOutParser::Message msg;
					if(!m_midiOutParser.feed(b, msg) || racing)
						continue;
					Event e;
					e.frame = produced - skew;
					e.size = msg.size;
					std::copy_n(msg.bytes, 3, e.bytes);
					m_midiOutQueue.push(e);	// dropped if the host has stopped taking blocks
				}
				pcOut.clear();
				mc.getPcPort().takeTx(pcOut);
				m_pcOut += pcOut.size();
				keepOwn(m_settings);
				keepOwn(m_presets);
				keepOwn(m_keeper);
				if(!osUp)
				{
					SynthSettings s;
					uint64_t revision = 0;
					osUp = m_settings->settings(s, revision);
				}
				lock.unlock();
				if(m_pcPort && !pcOut.empty())
					m_pcPort->send(m_pcIndex, pcOut);
				if(m_link && !pcOut.empty())
					m_link->send(pcOut);
				busy += std::chrono::duration<double>(clock::now() - t0).count();
			}

			const auto t1 = clock::now();
			if(t1 - lastStats >= std::chrono::milliseconds(500))
			{
				const double wall = std::chrono::duration<double>(t1 - lastStats).count();
				const auto cycles = mc.ucCycles();
				std::lock_guard<std::mutex> statsLock(m_statsMutex);
				m_stats.seconds += wall;
				m_stats.speed = 100.0 * static_cast<double>(cycles - lastCycles) / g1::g_ucClock / wall;
				m_stats.load = 100.0 * busy / wall;
				m_stats.dspCount = mc.dspCount();
				for(uint32_t d = 0; d < mc.dspCount(); ++d)
				{
					m_stats.dspOn[d] = mc.getDsp(d).booted();
					m_stats.dspFailed[d] = mc.getDsp(d).jitFailed();
					if(mc.getDsp(d).jitFailed() && m_stats.dspProblem.empty())
					{
						m_stats.dspProblem = "DSP " + std::to_string(d) + " stopped: the JIT could not generate its code (" + mc.getDsp(d).jitFailure() + ")";
						g1app::logEvent(m_stats.dspProblem);
					}
				}
				lastCycles = cycles;
				lastStats = t1;
				busy = 0;

				// Dropouts (issues #63, #65): the first one, then again every 100, with the load at
				// the time -- a user's own "it was fine, then it wasn't" needs this to mean anything.
				if(const auto xruns = m_xruns.load(std::memory_order_relaxed); xruns > 0
					&& (m_loggedXruns == 0 || xruns / 100 > m_loggedXruns / 100))
				{
					m_loggedXruns = xruns;
					g1app::logEvent("dropout: " + std::to_string(xruns) + " total, load "
						+ std::to_string(static_cast<int>(m_stats.load)) + "%");
				}
			}
		}
	}

	HostStats Runner::stats()
	{
		std::lock_guard<std::mutex> lock(m_statsMutex);
		auto s = m_stats;
		s.midiIn = m_midiIn;
		s.midiOut = m_midiOut;
		s.pcIn = m_pcIn;
		s.pcOut = m_pcOut;
		s.xruns = m_xruns;
		s.peak = m_bridge.peak();
		return s;
	}
}
