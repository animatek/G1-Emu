#include "miditransport.h"

// The only place that knows which MIDI transports exist, and which one this build uses.
#ifdef G1_BACKEND_JUCE
#ifdef G1_WINDOWS_MIDI
#include "windowsmidi.h"
#else
#include "jucemidi.h"
#endif
#else
#include "alsamidi.h"
#endif

namespace g1app
{
	std::unique_ptr<MidiTransport> makeMidiTransport(const char* _clientName, std::string& _log)
	{
#ifdef G1_BACKEND_JUCE
#ifdef G1_WINDOWS_MIDI
		// Windows MIDI Services. When the service cannot be reached the transport still comes
		// back: the emulator runs without MIDI, and describe() gives the reason.
		auto midi = std::make_unique<WindowsMidi>(_clientName);
		if(!midi->valid())
			_log += "Windows MIDI Services: " + midi->error() + "\n";
		return midi;
#else
		return std::make_unique<JuceMidi>(_clientName);
#endif
#else
		auto midi = std::make_unique<AlsaMidi>(_clientName);
		if(!midi->valid())
		{
			_log += "cannot open the ALSA sequencer\n";
			return nullptr;
		}
		return midi;
#endif
	}
}
