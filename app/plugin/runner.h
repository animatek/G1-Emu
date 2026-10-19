#pragma once

// Runner: an Engine driven by a DAW's audio blocks instead of the wall clock.
//
// A worker thread keeps the emulator exactly `latency()` frames ahead of what the host has taken,
// and stops there. The audio callback only takes finished frames out of a lock-free ring: it never
// runs the emulator, never locks and never waits (except when rendering offline, where waiting is
// the point). The latency is reported to the host, which delays everything else to match.
//
// Because the emulator never gets further ahead than that, a MIDI event at offset o of the block
// that starts at host frame C can be given to the G1 at exactly frame C + latency + o of its
// output: the notes land where the DAW put them, shifted by the reported latency, whatever the
// block size. (Then the G1's MIDI IN takes its 1 ms per three bytes, like the hardware.)
//
// If the worker falls behind anyway, the callback puts out silence for the missing frames and
// throws their late copies away when they arrive, so the timing stays locked to the host.

#include "audiobridge.h"
#include "diaglog.h"
#include "directlink.h"
#include "engine.h"
#include "hostconfig.h"
#include "miditransport.h"
#include "midirouting.h"
#include "presetslink.h"
#include "slotkeeper.h"
#include "synthsettings.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace g1app
{
	// One producer, one consumer, fixed size, and the consumer can look before it takes.
	template<typename T, size_t N> class SpscQueue
	{
		static_assert((N & (N - 1)) == 0, "a power of two");
	public:
		bool push(const T& _v)
		{
			const auto w = m_write.load(std::memory_order_relaxed);
			if(w - m_read.load(std::memory_order_acquire) == N)
				return false;
			m_data[w & (N - 1)] = _v;
			m_write.store(w + 1, std::memory_order_release);
			return true;
		}
		const T* front() const
		{
			const auto r = m_read.load(std::memory_order_relaxed);
			return r == m_write.load(std::memory_order_acquire) ? nullptr : &m_data[r & (N - 1)];
		}
		void pop() { m_read.store(m_read.load(std::memory_order_relaxed) + 1, std::memory_order_release); }
	private:
		std::array<T, N> m_data{};
		std::atomic<size_t> m_write{0}, m_read{0};
	};

	class Runner
	{
	public:
		// Starts the worker. _maxBlock is the largest block the host announced; the latency is that
		// plus a margin for the worker's own unevenness (G1_PLUGIN_MARGIN_MS, 5 ms by default) and
		// a guard of 0.5 ms that keeps every event ahead of the emulator.
		// _pcPort, if given, carries the G1's PC Port (an editor's SysEx) on its port _pcIndex:
		// the worker polls it and sends the replies. _keeper, if given, talks to the PC Port too,
		// between the editor's messages (SlotKeeper), and so do _settings (the synth settings) and
		// _presets (the banks, for the Presets page). _link, if given, is the direct link to Animatek
		// NME (directlink.h): one more way in to the same PC Port, polled by the worker alongside it.
		// All must outlive the runner.
		Runner(Engine& _engine, double _rate, size_t _maxBlock, float _gainDb,
			MidiTransport* _pcPort = nullptr, int _pcIndex = 0, SlotKeeper* _keeper = nullptr,
			SynthSettingsLink* _settings = nullptr, DirectLink* _link = nullptr, PresetsLink* _presets = nullptr);
		~Runner();	// stops the worker and detaches from the engine; the engine keeps its state

		size_t latency() const { return m_latency; }
		// The last MIDI controller from the host, for the panel's MIDI Learn (MidiRouting). Any thread.
		bool lastController(int& _channel, int& _cc, uint32_t& _serial) const { return m_midiRouting.lastController(_channel, _cc, _serial); }
		// MIDI CC Mode on Selected Slots: a controller on a selected slot's channel goes to every selected
		// slot (MidiRouting). Any thread.
		void setSelectedControllers(const bool _on) { m_selectedControllers = _on; }
		// Octave Shift: On moves each note by its slot's shift, Off plays it as the rack does. Any thread.
		void setOctaveShift(const bool _on) { m_octaveShift = _on; }
		// While *_flag is set (the processor's restore of a project, its notes held
		// back), the worker races too. Before the worker starts, or never.
		void watchRestore(const std::atomic<bool>* _flag) { m_restoreFlag = _flag; }
		double rate() const { return m_rate; }

		// Audio thread. First the block's MIDI (offsets within the block), then the block itself.
		// Only short messages: SysEx from the track is not passed on in this version.
		void queueMidi(uint32_t _offset, const uint8_t* _data, size_t _size);
		void process(float* const* _outs, size_t _numOuts, const float* const* _ins, size_t _numIns, size_t _frames, bool _offline);

		void setGainDb(float _gainDb);

		// Held by the worker while it emulates: whoever wants to read the G1's state from
		// another thread (the flash, to save it) takes it for as long as it reads.
		std::unique_lock<std::mutex> lockEngine() { return std::unique_lock<std::mutex>(m_engineMutex); }

		HostStats stats();

	private:
		struct Event
		{
			uint64_t frame = 0;		// the output frame at which the G1 gets it
			uint8_t size = 0;
			uint8_t bytes[3] = {};
		};

		void run();

		Engine& m_engine;
		MidiTransport* const m_pcPort;
		DirectLink* const m_link;
		const int m_pcIndex;
		SlotKeeper* const m_keeper;
		SynthSettingsLink* const m_settings;
		PresetsLink* const m_presets;
		const double m_rate;
		const size_t m_ahead;		// how far the worker may run past what the host has taken
		const size_t m_latency;		// where the events go: ahead plus a guard (runner.cpp)
		AudioBridge m_bridge;
		HealthProbe m_health;			// worker thread, and DSP 3's for output()
		SpscQueue<Event, 1024> m_events;
		MidiRouting m_midiRouting;		// the worker's: the track's notes, by each slot's octave shift
		std::atomic<bool> m_selectedControllers{false};	// MIDI CC Mode on Selected Slots (setSelectedControllers)
		std::atomic<bool> m_octaveShift{false};			// Octave Shift On (setOctaveShift)
		std::vector<uint8_t> m_routed;
		std::vector<uint8_t> m_editorHeld;	// worker thread: the editor's bytes, held while the keeper uploads a slot

		std::atomic<uint64_t> m_consumed{0};	// frames the host has taken (audio thread writes)
		uint64_t m_debt = 0;					// frames owed: put out as silence, not yet arrived
		bool m_primed = false;					// the first full block has gone out
		std::atomic<const std::atomic<bool>*> m_restoreFlag{nullptr};
		std::atomic<size_t> m_discard{0};		// the raced frames still queued, for the audio thread to drop
		std::atomic<bool> m_raceEnded{false};	// for the audio thread: drop those and forgive m_debt

		std::mutex m_engineMutex;
		std::thread m_thread;
		std::atomic<bool> m_quit{false};

		std::mutex m_statsMutex;
		HostStats m_stats;
		std::atomic<uint64_t> m_midiIn{0}, m_midiOut{0}, m_pcIn{0}, m_pcOut{0}, m_xruns{0}, m_dropped{0};
		uint64_t m_loggedXruns = 0;				// worker thread only: dropouts already written to the log
	};
}
