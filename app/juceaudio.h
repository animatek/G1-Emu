#pragma once

// Audio through JUCE, which is how the G1 will reach macOS and Windows: CoreAudio there,
// WASAPI/ASIO/DirectSound on Windows, and ALSA or JACK on Linux, all behind one
// AudioDeviceManager. The work of converting rates and crossing threads is AudioBridge's, the
// same one the native JACK backend uses, so both sound alike by construction.
//
// It asks for the back panel's four outputs and two inputs and settles for what the card gives:
// on a stereo card the G1's outputs 1 and 2 come out and 3/4 are dropped, which is what the
// hardware's headphone socket does anyway.

#include "audiobridge.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <memory>
#include <string>
#include <vector>

namespace g1app
{
	class JuceAudio final : private juce::AudioIODeviceCallback
	{
	public:
		// _device is empty for the system default, or the name of one to open. _autoConnect is
		// unused here: there is no graph to patch into, the device is simply opened.
		JuceAudio(const std::string& _device, const float _gain, const std::string& _type = {})
			: m_bridge(_gain)
		{
			m_manager = std::make_unique<juce::AudioDeviceManager>();

			juce::String error;
			if(_device.empty() && _type.empty())
			{
				// The default input and the default output can be two different physical
				// devices: an Intel Mac's own speakers and its own microphone already are
				// ("Built-in Output" and "Built-in Microphone", not one "Built-in" device).
				// CoreAudio then needs an AudioIODeviceCombiner to bridge them, and its
				// start() can hang forever waiting for both HAL I/O threads to report ready
				// -- a real deadlock, seen on Intel Macs, that is JUCE's and not something to
				// paper over from here. So the two are only asked for together when they are
				// the same device, which is when combining never happens; otherwise this
				// settles for the output alone, which every card has, and drops the inputs.
				error = m_manager->initialiseWithDefaultDevices(
					sameDefaultInputAndOutput() ? static_cast<int>(AudioBridge::Ins) : 0,
					static_cast<int>(AudioBridge::Outs));
				// A card with fewer than four outputs: ask again for a plain stereo pair.
				if(error.isNotEmpty())
					error = m_manager->initialiseWithDefaultDevices(0, 2);
			}
			else
			{
				juce::AudioDeviceManager::AudioDeviceSetup setup;
				setup.outputDeviceName = _device;
				setup.inputDeviceName = _device;
				error = m_manager->initialise(static_cast<int>(AudioBridge::Ins),
					static_cast<int>(AudioBridge::Outs), nullptr, true, _type, &setup);
				// _device may name a device good for output only (or only for input): on a
				// split-device Mac "Built-in Output" answers to no input by that name. Settle
				// for output only rather than refusing to make any sound at all.
				if(error.isNotEmpty())
				{
					juce::AudioDeviceManager::AudioDeviceSetup outOnly;
					outOnly.outputDeviceName = _device;
					error = m_manager->initialise(0, static_cast<int>(AudioBridge::Outs), nullptr, true, _type, &outOnly);
				}
			}
			if(error.isNotEmpty())
			{
				m_error = error.toStdString();
				m_manager.reset();
				return;
			}

			if(auto* dev = m_manager->getCurrentAudioDevice())
			{
				m_bridge.setRate(static_cast<uint32_t>(dev->getCurrentSampleRate()));
				m_name = dev->getName().toStdString();
				m_outs = static_cast<size_t>(dev->getActiveOutputChannels().countNumberOfSetBits());
			}
			m_manager->addAudioCallback(this);
		}

		~JuceAudio() override
		{
			if(m_manager)
			{
				m_manager->removeAudioCallback(this);
				m_manager->closeAudioDevice();
			}
		}

		bool valid() const { return m_manager != nullptr && m_manager->getCurrentAudioDevice() != nullptr; }
		const std::string& error() const { return m_error; }
		const std::string& deviceName() const { return m_name; }
		size_t outputs() const { return m_outs; }

		uint32_t rate() const { return m_bridge.rate(); }
		uint64_t xruns() const { return m_bridge.xruns(); }
		float peak() { return m_bridge.peak(); }
		void setGain(const float _gain) { m_bridge.setGain(_gain); }
		void push(int32_t _o1, int32_t _o2, int32_t _o3, int32_t _o4) { m_bridge.push(_o1, _o2, _o3, _o4); }
		void pullInput(int32_t& _l, int32_t& _r) { m_bridge.pullInput(_l, _r); }

		// Every device JUCE can see, as "type: name", for the settings window.
		static std::vector<std::string> devices()
		{
			std::vector<std::string> out;
			juce::AudioDeviceManager manager;
			juce::AudioDeviceManager::AudioDeviceSetup setup;
			manager.initialise(0, 2, nullptr, false, {}, &setup);
			for(auto* type : manager.getAvailableDeviceTypes())
			{
				type->scanForDevices();
				for(const auto& name : type->getDeviceNames(false))
					out.push_back(type->getTypeName().toStdString() + ": " + name.toStdString());
			}
			return out;
		}

	private:
		// Whether the default input and the default output are the same physical device: true
		// on most cards (one name for both) and on Apple Silicon Macs, false on an Intel Mac's
		// own built-in audio (see the constructor). Answered without opening anything, so it is
		// safe to call before the device is chosen.
		bool sameDefaultInputAndOutput() const
		{
			m_manager->getAvailableDeviceTypes();	// scans, so getCurrentDeviceTypeObject() has data
			auto* type = m_manager->getCurrentDeviceTypeObject();
			if(!type)
				return true;
			const auto inNames = type->getDeviceNames(true);
			const auto outNames = type->getDeviceNames(false);
			const auto inIndex = type->getDefaultDeviceIndex(true);
			const auto outIndex = type->getDefaultDeviceIndex(false);
			if(inIndex < 0 || inIndex >= inNames.size() || outIndex < 0 || outIndex >= outNames.size())
				return true;
			return inNames[inIndex] == outNames[outIndex];
		}

		void audioDeviceIOCallbackWithContext(const float* const* _in, const int _numIn,
			float* const* _out, const int _numOut, const int _frames,
			const juce::AudioIODeviceCallbackContext&) override
		{
			m_bridge.render(_out, static_cast<size_t>(_numOut), _in, static_cast<size_t>(_numIn),
				static_cast<size_t>(_frames));
		}

		void audioDeviceAboutToStart(juce::AudioIODevice* _device) override
		{
			m_bridge.setRate(static_cast<uint32_t>(_device->getCurrentSampleRate()));
		}

		void audioDeviceStopped() override {}

		AudioBridge m_bridge;
		std::unique_ptr<juce::AudioDeviceManager> m_manager;
		std::string m_error, m_name;
		size_t m_outs = 0;
	};
}
