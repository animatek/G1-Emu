#include "g1thread.h"

#include "dsp56kBase/threadtools.h"

#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// Both appeared in Windows 10 (1803 and 1709); older SDKs lack the names, not the calls.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#endif

namespace g1
{
#ifdef _WIN32
	namespace
	{
		// One per thread, made on its first sleep and closed when the thread ends.
		struct Timer
		{
			HANDLE handle = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
			~Timer()
			{
				if(handle)
					CloseHandle(handle);
			}
		};
	}
#endif

	void preciseSleep(const std::chrono::microseconds _duration)
	{
#ifdef _WIN32
		thread_local Timer timer;
		if(timer.handle)
		{
			LARGE_INTEGER due;
			due.QuadPart = -static_cast<LONGLONG>(_duration.count()) * 10;	// relative, in 100 ns
			if(SetWaitableTimerEx(timer.handle, &due, 0, nullptr, nullptr, nullptr, 0))
			{
				WaitForSingleObject(timer.handle, INFINITE);
				return;
			}
		}
		// Before Windows 10 1803 there is no such timer: the 15.6 ms sleep is what is left.
#endif
		std::this_thread::sleep_for(_duration);
	}

	void keepThreadAtFullSpeed()
	{
#if defined(_WIN32) && defined(THREAD_POWER_THROTTLING_CURRENT_VERSION)
		THREAD_POWER_THROTTLING_STATE state{};
		state.Version = THREAD_POWER_THROTTLING_CURRENT_VERSION;
		state.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
		state.StateMask = 0;	// controlled by us, and off
		SetThreadInformation(GetCurrentThread(), ThreadPowerThrottling, &state, sizeof(state));
#endif
		dsp56k::ThreadTools::setCurrentThreadPriority(dsp56k::ThreadPriority::Highest);	// false where not allowed: it stays as it was
	}
}
