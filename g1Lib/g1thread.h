#pragma once

#include <chrono>

// What the emulator's threads need from the OS beyond std::thread (issues #63, #65).
namespace g1
{
	// A sleep that lasts about what it is asked for. On Windows std::this_thread::sleep_for(200 us)
	// takes the system timer's period, 15.6 ms unless some program in the same process asked for
	// more (a DAW often does, a plugin host process may not): a worker allowed to run only a few ms
	// ahead of the audio then falls behind it at every sleep, and the host hears dropouts. A
	// high-resolution waitable timer there wakes after ~0.5 ms, with no change to the system's timer.
	void preciseSleep(std::chrono::microseconds _duration);

	// For a thread that has to keep up with the audio: on Windows 11 a thread of a process in the
	// background, or on battery or a power-saving plan, can be put under EcoQoS, which runs it slower
	// and, on a hybrid CPU, on the efficiency cores. This opts the calling thread out. And everywhere
	// it raises the thread's priority as Gearmulator raises its emulation threads' (dsp56k::ThreadTools,
	// Highest): nice -10 on Linux, where the system allows it (RLIMIT_NICE), user-interactive QoS on
	// macOS, time-critical on Windows. At the normal priority a DAW's other plugins and threads took
	// the emulation's turn and it dropped out with the load well below 100 % (Bitwig, 2026-10-18).
	void keepThreadAtFullSpeed();
}
