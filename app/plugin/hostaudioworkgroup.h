#pragma once

// HostAudioWorkgroup: on macOS, hands the IO workgroup of the host's audio device to the emulation
// threads (#81, #85; docs/stability.md S4). Ported from Gearmulator 78766b46 (BUG-10380,
// pluginLib::HostAudioWorkgroup).
//
// On Apple Silicon a thread outside the host's audio workgroup is scheduled by the load of the rest
// of its process: while the host looks idle it stays on the efficiency cores, and the host waits for
// audio that comes late. Dropouts in the plugin and not in the standalone, worse on macOS 27, with
// the load well below 100 %. Members of the workgroup count as part of the host's audio deadline.
//
// Only an AU can ask the host for its workgroup. But the host runs its audio device in its own
// process, the one the plugin lives in, and CoreAudio says which device runs here: this looks for it
// once a second (the user switches devices, the host stops and starts its own) and sets it with
// dsp56k::AudioWorkgroup::set. The threads join it with an AudioWorkgroup::Member: the DSP workers
// (g1::Microcontroller::workerLoop) and the runner's worker. The setting is process-wide, so every
// instance's timer sets the same one, which is a no-op after the first.
//
// Does nothing elsewhere.

#include <juce_events/juce_events.h>

namespace g1app
{
	class HostAudioWorkgroup : juce::Timer
	{
	public:
		HostAudioWorkgroup();
		~HostAudioWorkgroup() override;

	private:
		void timerCallback() override;
	};
}
