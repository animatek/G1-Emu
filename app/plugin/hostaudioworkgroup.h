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
// process, the one the plugin lives in, and CoreAudio says which device runs here: poll() looks for
// it and sets it with dsp56k::AudioWorkgroup::set. The Processor calls it from its timer about once a
// second (the user switches devices, the host stops and starts its own). The threads join it with an
// AudioWorkgroup::Member: the DSP workers (g1::Microcontroller::workerLoop) and the runner's worker.
// The setting is process-wide, so every instance sets the same one, a no-op after the first.
//
// No JUCE here on purpose: a JUCE header included ahead of the Processor's own broke the Windows
// build (Windows' `small` macro in juce_PushNotifications.h). Does nothing off macOS.

namespace g1app
{
	class HostAudioWorkgroup
	{
	public:
		HostAudioWorkgroup() { poll(); }	// the first instance's threads join from the start
		void poll();
	};
}
