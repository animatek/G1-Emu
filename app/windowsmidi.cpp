#include "windowsmidi.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Midi2.h>
#include <winrt/Windows.Devices.Midi2.Enumeration.h>
#include <winrt/Windows.Devices.Midi2.Transports.Virtual.h>

#include <array>
#include <objbase.h>
#include <condition_variable>
#include <deque>
#include <future>
#include <mutex>
#include <thread>

namespace g1app
{
	namespace midi = winrt::Windows::Devices::Midi2;
	namespace virtualMidi = midi::Transports::Virtual;

	struct WindowsMidi::Impl
	{
		struct Port
		{
			explicit Port(uint8_t group) : transmit(group, juce::ump::PacketProtocol::MIDI_1_0, 4096) {}
			std::mutex mutex;
			std::vector<uint8_t> incoming;
			juce::ump::ToBytestreamDispatcher receive{4096};
			juce::ump::BytestreamToUMPDispatcher transmit;
		};

		std::string clientName, error;
		midi::MidiSession session{nullptr};
		virtualMidi::MidiVirtualDevice device{nullptr};
		midi::MidiEndpointConnection connection{nullptr};
		winrt::event_token token{};
		std::vector<std::shared_ptr<Port>> ports;
		std::vector<std::string> portNames;
		std::mutex workMutex;
		std::condition_variable workReady;
		std::deque<std::function<void()>> work;
		bool stopping = false;
		std::thread worker;

		Impl() : worker([this]
		{
			// JUCE's GUI thread is STA. Keep SDK objects in an MTA so MIDI callbacks and
			// teardown do not depend on the GUI pumping messages during startup/shutdown.
			const auto hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
			for(;;)
			{
				std::function<void()> task;
				{
					std::unique_lock<std::mutex> lock(workMutex);
					workReady.wait(lock, [this] { return stopping || !work.empty(); });
					if(stopping && work.empty()) break;
					task = std::move(work.front());
					work.pop_front();
				}
				task();
			}
			if(SUCCEEDED(hr)) CoUninitialize();
		}) {}

		void invoke(std::function<void()> action)
		{
			auto task = std::make_shared<std::packaged_task<void()>>(std::move(action));
			auto done = task->get_future();
			{
				std::lock_guard<std::mutex> lock(workMutex);
				work.emplace_back([task] { (*task)(); });
			}
			workReady.notify_one();
			done.get();
		}

		~Impl()
		{
			invoke([this]
			{
			// Callbacks hold a shared Port, never the owner. Close the session before releasing
			// ports so even an in-flight callback can finish without accessing freed memory.
			try
			{
				if(connection && token.value)
					connection.MessageReceived(token);
				if(session)
					session.Close();
			}
			catch(...) {}
			ports.clear();
			session = nullptr;
			});
			{
				std::lock_guard<std::mutex> lock(workMutex);
				stopping = true;
			}
			workReady.notify_one();
			worker.join();
		}
	};

	WindowsMidi::WindowsMidi(const char* clientName) : m_impl(std::make_unique<Impl>())
	{
		m_impl->clientName = clientName;
		if(const char* enabled = std::getenv("G1_WINDOWS_MIDI"); enabled && std::string(enabled) == "0")
		{
			m_impl->error = "disabled by G1_WINDOWS_MIDI=0";
			return;
		}
		m_impl->invoke([this, clientName]
		{
		try
		{
			if(!midi::MidiApi::EnsureServiceAvailable())
				throw std::runtime_error("Windows MIDI Services is unavailable");
			if(!virtualMidi::MidiVirtualDeviceManager::IsTransportAvailable())
				throw std::runtime_error("Windows MIDI Services virtual transport is unavailable");
			m_impl->session = midi::MidiSession::Create(winrt::to_hstring(clientName));
			if(!m_impl->session)
				throw std::runtime_error("Cannot create Windows MIDI Services session");
		}
		catch(const winrt::hresult_error& e) { m_impl->error = winrt::to_string(e.message()); }
		catch(const std::exception& e) { m_impl->error = e.what(); }
		});
	}

	WindowsMidi::~WindowsMidi() = default;
	bool WindowsMidi::valid() const { return m_impl->session != nullptr; }
	const std::string& WindowsMidi::error() const { return m_impl->error; }
	bool WindowsMidi::virtualPorts() const
	{
		return valid() && m_impl->ports.size() == 2 && m_impl->connection != nullptr;
	}

	int WindowsMidi::addPort(const char* name)
	{
		const int index = static_cast<int>(m_impl->ports.size());
		m_impl->ports.push_back(std::make_shared<Impl::Port>(static_cast<uint8_t>(index)));
		m_impl->portNames.emplace_back(name);
		// The G1 is one instrument with two MIDI 1.0 ports, represented by two groups.
		// Creating a separate virtual device for each port also hits a known 26100.8875
		// service lifecycle bug on the second CreateVirtualDevice call.
		if(m_impl->ports.size() != 2)
			return index;
		m_impl->invoke([this]
		{
		try
		{
			if(!valid()) return;
			const auto fullName = winrt::to_hstring(m_impl->clientName);
			midi::Enumeration::MidiDeclaredEndpointInfo info;
			info.Name(fullName);
			// Windows software-device identifiers must not contain display-name spaces.
			const auto productId = juce::String(m_impl->clientName)
				.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-");
			info.ProductInstanceId(winrt::to_hstring(productId.toStdString()));
			info.HasStaticFunctionBlocks(true);
			info.SupportsMidi10Protocol(true);
			info.SupportsMidi20Protocol(false);
			info.SpecificationVersionMajor(1);
			info.SpecificationVersionMinor(1);
			virtualMidi::MidiVirtualDeviceCreationConfig config(fullName, L"G1-Emu virtual MIDI port", L"Animatek", info);
			for(size_t i = 0; i < m_impl->portNames.size(); ++i)
			{
				midi::Enumeration::MidiFunctionBlock block;
				block.Number(static_cast<uint8_t>(i));
				block.IsActive(true);
				block.Name(winrt::to_hstring(m_impl->portNames[i]));
				block.FirstGroup(midi::MidiGroup(static_cast<uint8_t>(i)));
				block.GroupCount(1);
				block.Direction(midi::Enumeration::MidiFunctionBlockDirection::Bidirectional);
				block.RepresentsMidi10Connection(midi::Enumeration::MidiFunctionBlockRepresentsMidi10Connection::YesBandwidthUnrestricted);
				config.FunctionBlocks().Append(block);
			}
			m_impl->device = virtualMidi::MidiVirtualDeviceManager::CreateVirtualDevice(config);
			if(!m_impl->device) throw std::runtime_error("Virtual MIDI device creation failed");
			m_impl->connection = m_impl->session.CreateEndpointConnection(m_impl->device.DeviceEndpointDeviceId());
			if(!m_impl->connection) throw std::runtime_error("Virtual MIDI connection creation failed");
			m_impl->token = m_impl->connection.MessageReceived([this](const auto&, const midi::MidiMessageReceivedEventArgs& args)
			{
				std::array<uint32_t, 4> words{};
				args.FillWordArray(0, words);
				const juce::ump::View packet(words.data());
				const auto group = static_cast<size_t>((words[0] >> 24) & 0x0f);
				if(group < m_impl->ports.size())
				{
					auto& active = *m_impl->ports[group];
					std::lock_guard<std::mutex> lock(active.mutex);
					active.receive.dispatch({words.data(), packet.size()}, 0, [&](const juce::ump::BytesOnGroup& msg, double)
					{
						const auto* bytes = reinterpret_cast<const uint8_t*>(msg.bytes.data());
						active.incoming.insert(active.incoming.end(), bytes, bytes + msg.bytes.size());
					});
				}
			});
			if(m_impl->connection.AddMessageProcessingPlugin(m_impl->device) != midi::MidiMessageProcessingPluginAddResult::Succeeded)
				throw std::runtime_error("Virtual MIDI processing plugin failed");
			if(!m_impl->connection.Open()) throw std::runtime_error("Virtual MIDI connection could not open");
		}
		catch(const winrt::hresult_error& e)
		{
			m_impl->error = winrt::to_string(e.message());
			m_impl->connection = nullptr;
		}
		catch(const std::exception& e)
		{
			m_impl->error = e.what();
			m_impl->connection = nullptr;
		}
		});
		return index;
	}

	void WindowsMidi::poll(std::vector<std::vector<uint8_t>>& perPort)
	{
		perPort.resize(m_impl->ports.size());
		for(size_t i = 0; i < perPort.size(); ++i)
		{
			auto& port = *m_impl->ports[i];
			std::lock_guard<std::mutex> lock(port.mutex);
			perPort[i].insert(perPort[i].end(), port.incoming.begin(), port.incoming.end());
			port.incoming.clear();
		}
	}

	void WindowsMidi::send(int index, const std::vector<uint8_t>& bytes)
	{
		if(index < 0 || static_cast<size_t>(index) >= m_impl->ports.size() || bytes.empty()) return;
		auto& port = *m_impl->ports[static_cast<size_t>(index)];
		if(!m_impl->connection) return;
		port.transmit.dispatch({reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()}, 0,
			[&](const juce::ump::View& packet, double)
			{
				m_impl->connection.SendSingleMessageWordArray(0, 0, static_cast<uint8_t>(packet.size()),
					winrt::array_view<const uint32_t>(packet.begin(), packet.end()));
			});
	}
}
