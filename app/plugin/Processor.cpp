#include "Processor.h"

#include "Editor.h"

#include "romfinder.h"
#include "g1Lib/g1format.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <utility>

namespace g1plugin
{
	namespace
	{
		constexpr const char* g_stateTag = "G1EmuState";
		constexpr int g_stateVersion = 1;
		constexpr int g_stereoBuses = 2;	// Out 1/2 and Out 3/4; after them, Out 1..4 mono

		juce::String packBytes(const std::vector<uint8_t>& _data)
		{
			juce::MemoryOutputStream packed;
			{
				juce::GZIPCompressorOutputStream zip(packed, 9);
				zip.write(_data.data(), _data.size());
			}
			return packed.getMemoryBlock().toBase64Encoding();
		}

		std::vector<uint8_t> toBytes(const juce::MemoryBlock& _block)
		{
			const auto* p = static_cast<const uint8_t*>(_block.getData());
			return std::vector<uint8_t>(p, p + _block.getSize());
		}

		bool unpackBytes(const juce::String& _text, std::vector<uint8_t>& _data)
		{
			juce::MemoryBlock packed;
			if(!packed.fromBase64Encoding(_text))
				return false;
			juce::MemoryInputStream in(packed, false);
			juce::GZIPDecompressorInputStream zip(in);
			juce::MemoryBlock plain;
			juce::MemoryOutputStream out(plain, false);
			out.writeFromInputStream(zip, -1);
			out.flush();
			_data = toBytes(plain);
			return true;
		}

		// The numbers of the live instances, so each PC Port gets a name of its own and a freed
		// number is reused (close the second of three and the next one is 2 again). A host can put
		// each instance in a process of its own (Bitwig does), so a number is held with a lock
		// every process sees (a file lock: the system lets it go if the process dies), kept for
		// the instance's life. JUCE's list of MIDI ports is no use for this: another process's
		// new port reaches it late. It is checked as well, for ports made some other way.
		std::mutex g_instancesMutex;
		std::set<int> g_instances;

		std::string pcPortClient(const int _n)
		{
			return _n == 1 ? std::string("G1-Emu") : "G1-Emu " + std::to_string(_n);
		}

		int takeInstanceNumber(std::unique_ptr<juce::InterProcessLock>& _hold)
		{
			std::set<juce::String> taken;
			for(const auto& d : juce::MidiInput::getAvailableDevices())
				taken.insert(d.name);
			for(const auto& d : juce::MidiOutput::getAvailableDevices())
				taken.insert(d.name);
			std::lock_guard<std::mutex> lock(g_instancesMutex);
			for(int n = 1;; ++n)
			{
				if(g_instances.count(n) || taken.count(juce::String(pcPortClient(n) + " PC Port")))
					continue;
				auto hold = std::make_unique<juce::InterProcessLock>("G1-Emu PC Port " + juce::String(n));
				if(!hold->enter(0))
					continue;
				_hold = std::move(hold);
				g_instances.insert(n);
				return n;
			}
		}

		void releaseInstanceNumber(const int _n)
		{
			std::lock_guard<std::mutex> lock(g_instancesMutex);
			g_instances.erase(_n);
		}
	}

	// ____________________________________________________________________________________________
	// The knobs as parameters

	KnobParameter::KnobParameter(const int _index)
		: juce::AudioParameterFloat(juce::ParameterID{"knob" + juce::String(_index + 1), 1}, "Knob " + juce::String(_index + 1),
			juce::NormalisableRange<float>(0.0f, 1.0f, 1.0f / 253.0f), 0.0f),	// the knob's 254 positions
		  m_index(_index), m_name("Knob " + juce::String(_index + 1))
	{
	}

	juce::String KnobParameter::getName(const int _maxLength) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		return m_name.substring(0, _maxLength);
	}

	bool KnobParameter::setInfo(const g1::KnobInfo& _info)
	{
		juce::String name = "Knob " + juce::String(m_index + 1);
		if(_info.assigned)
			name << ": " << juce::String(_info.moduleName) << " " << juce::String(_info.paramName);
		std::lock_guard<std::mutex> lock(m_mutex);
		m_info = _info;
		if(name == m_name)
			return false;
		m_name = name;
		return true;
	}

	juce::String KnobParameter::getText(const float _v, const int _maxLength) const
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		// value = position * (max + 1) / 256, as the OS takes it (g1knobs.h, positionFor), read
		// as the editor shows it ("Sine", "1.25 kHz": g1format.h)
		if(m_info.assigned && m_info.section != 2 && m_info.max > 0)
			return juce::String(g1::formatValue(m_info.type, m_info.param, toAdc(_v) * (m_info.max + 1) / 256)).substring(0, _maxLength);
		return juce::String(juce::roundToInt(_v * 100.0f)) + "%";
	}

	// The way back of getText: a percentage, the text of one of the values ("Saw"), or the OS's
	// number for the value of the parameter the knob moves.
	float KnobParameter::getValueForText(const juce::String& _text) const
	{
		const auto t = _text.trim();
		const float n = t.getFloatValue();
		std::lock_guard<std::mutex> lock(m_mutex);
		if(t.endsWithChar('%') || !m_info.assigned || m_info.section == 2 || m_info.max == 0)
			return juce::jlimit(0.0f, 1.0f, n / 100.0f);
		for(int v = 0; v <= m_info.max; ++v)
			if(t.equalsIgnoreCase(juce::String(g1::formatValue(m_info.type, m_info.param, v))))
				return toParam(g1::KnobMap::positionFor(static_cast<uint8_t>(v), m_info.max));
		const auto value = static_cast<uint8_t>(juce::jlimit(0, static_cast<int>(m_info.max), juce::roundToInt(n)));
		return toParam(g1::KnobMap::positionFor(value, m_info.max));
	}

	Processor::Processor()
		: juce::AudioProcessor(BusesProperties()
			.withInput("In L/R", juce::AudioChannelSet::stereo(), false)
			.withOutput("Out 1/2", juce::AudioChannelSet::stereo(), true)
			.withOutput("Out 3/4", juce::AudioChannelSet::stereo(), true)
			// The same four outputs one by one, for hosts that route mono channels (issue #27).
			// Off until the host turns them on (Cubase's Activate Outputs).
			.withOutput("Out 1", juce::AudioChannelSet::mono(), false)
			.withOutput("Out 2", juce::AudioChannelSet::mono(), false)
			.withOutput("Out 3", juce::AudioChannelSet::mono(), false)
			.withOutput("Out 4", juce::AudioChannelSet::mono(), false))
	{
		for(int k = 0; k < 18; ++k)
		{
			auto param = std::make_unique<KnobParameter>(k);
			m_knobParams[static_cast<size_t>(k)] = param.get();
			addParameter(param.release());
		}
		auto volume = std::make_unique<VolumeParameter>();
		m_volumeParam = volume.get();
		addParameter(volume.release());
		loadPreferences();
		openPcPort();
		openLink();
		findRom();
		startTimerHz(20);
	}

	namespace
	{
		std::string preferencesPath()
		{
			return (std::filesystem::path(g1app::defaultSettingsPath()).parent_path() / "plugin.conf").string();
		}
	}

	// What the last editor was left like: a new instance starts there, and a project's state, when
	// one comes, has the last word. Plain "key = value", like the settings file.
	void Processor::loadPreferences()
	{
		std::ifstream f(preferencesPath());
		std::string line;
		while(std::getline(f, line))
		{
			const auto eq = line.find('=');
			if(eq == std::string::npos || line[0] == '#')
				continue;
			const auto key = juce::String(line.substr(0, eq)).trim();
			const auto value = juce::String(line.substr(eq + 1)).trim();
			if(key == "extrasOpen")				m_extrasOpen = value != "0";
			else if(key == "knobDisplays")		m_knobDisplays = value != "0";
			else if(key == "knobFollowsPatch")	m_knobFollowsPatch = value != "0";
			else if(key == "randomExclude")		m_randomExclude = g1app::knobListFromString(value.toStdString());
			else if(key == "panelScale")		m_panelScale = juce::jlimit(g1gui::PanelView::MinScale, g1gui::PanelView::MaxScale, value.getFloatValue());
		}
	}

	void Processor::savePreferences() const
	{
		std::error_code ec;
		std::filesystem::create_directories(std::filesystem::path(preferencesPath()).parent_path(), ec);
		std::ofstream f(preferencesPath(), std::ios::trunc);
		f << "# G1-Emu plugin: how a new instance's window starts (a project keeps its own).\n"
		  << "extrasOpen = " << (m_extrasOpen ? 1 : 0) << "\n"
		  << "knobDisplays = " << (m_knobDisplays ? 1 : 0) << "\n"
		  << "knobFollowsPatch = " << (m_knobFollowsPatch ? 1 : 0) << "\n"
		  << "randomExclude = " << g1app::knobListToString(m_randomExclude) << "\n"
		  << "panelScale = " << m_panelScale << "\n";
	}

	Processor::~Processor()
	{
		stopTimer();
		cancelPendingUpdate();
		// A turn still open ends, or the host keeps the parameter touched.
		std::vector<HostGestures<19>::Event> open;
		m_gestures.closeAll(open);
		for(const auto& e : open)
			(e.index == VolumeIndex ? static_cast<juce::RangedAudioParameter&>(*m_volumeParam)
				: static_cast<juce::RangedAudioParameter&>(*m_knobParams[e.index])).endChangeGesture();
		{
			std::lock_guard<std::mutex> lock(m_lifecycle);
			m_runner.reset();
			m_engine.reset();
		}
		m_pcPort.reset();
		m_link.stop();
		if(m_instance > 0)
			releaseInstanceNumber(m_instance);
		m_instanceLock.reset();
	}

	// The G1's PC Port as a virtual MIDI port, for the editor (#8 will add a direct link beside it).
	// G1_PLUGIN_PC_PORT=0 leaves it out.
	void Processor::openPcPort()
	{
		if(const char* v = std::getenv("G1_PLUGIN_PC_PORT"); v && *v == '0')
		{
			m_pcProblem = "off (G1_PLUGIN_PC_PORT=0)";
			return;
		}
		// JUCE's MIDI endpoints are a singleton that JUCE 8 never makes again once it has shut
		// down, and the CLAP wrapper shuts JUCE down with its last instance: the next instance in
		// the same process finds none, and would crash on it. (Holding JUCE up from here is worse:
		// it outlives the module when the host unloads it.)
		if(juce::ump::Endpoints::getInstance() == nullptr)
		{
			m_pcProblem = "none in this instance: JUCE's MIDI was shut down with the last instance in this "
				"process (CLAP). Reload the plugin, or keep one instance open, to have it";
			return;
		}
		m_instance = takeInstanceNumber(m_instanceLock);
		m_pcPort = std::make_unique<g1app::JuceMidi>(pcPortClient(m_instance).c_str());
		m_pcIndex = m_pcPort->addPort("PC Port");
	}

	void Processor::openLink()
	{
		if(const char* v = std::getenv("G1_DIRECT_LINK"); v && *v == '0')
		{
			m_linkProblem = "off (G1_DIRECT_LINK=0)";
			return;
		}
		g1app::HostOptions options;
		options.load(g1app::defaultSettingsPath());
		if(!options.directLink)
		{
			m_linkProblem = "off (directLink = 0 in the settings)";
			return;
		}
		if(!m_link.start("G1-Emu plugin", m_linkProblem))
			return;
		// Numbered by the port it got, which is what tells instances apart in the editor.
		const int n = m_link.port() - g1app::DirectLink::kBasePort;
		m_link.setName("G1-Emu plugin " + std::to_string(n + 1));
		if(m_pcPort && m_pcPort->virtualPorts())
		{
			m_link.setPcPortIds(m_pcPort->portIds(m_pcIndex));
			m_link.setPcPortName(m_pcPort->portListName(m_pcIndex));
		}
	}

	void Processor::findRom()
	{
		g1app::HostOptions options;
		options.load(g1app::defaultSettingsPath());
		if(const char* v = std::getenv("G1_ROM"))
			options.rom = v;
		const auto search = g1app::findRom({}, options.rom);
		std::vector<uint8_t> rom;
		if(search.found() && g1app::inspectRom(search.path, rom).ok())
		{
			std::string osNote;
			m_os = options.loadOs(osNote);
			m_rom = std::move(rom);
			m_romPath = search.path;
			m_romProblem.clear();
		}
		else
			m_romProblem = g1app::missingRomMessage(search);
	}

	void Processor::useRom(const juce::File& _file)
	{
		std::vector<uint8_t> rom;
		const auto check = g1app::inspectRom(_file.getFullPathName().toStdString(), rom);
		if(!check.ok())
		{
			m_romProblem = _file.getFullPathName().toStdString() + "\n    " + check.what() + "\n\n" + m_romProblem;
			++m_generation;		// the editor shows the new message
			return;
		}
		// Remembered for next time, in the same file and the same key the standalone uses.
		g1app::HostOptions options;
		options.load(g1app::defaultSettingsPath());
		options.rom = _file.getFullPathName().toStdString();
		options.save(g1app::defaultSettingsPath());

		std::string osNote;
		m_os = options.loadOs(osNote);
		m_rom = std::move(rom);
		m_romPath = options.rom;
		m_romProblem.clear();

		suspendProcessing(true);
		{
			std::lock_guard<std::mutex> lock(m_lifecycle);
			createEngine(m_haveState ? &m_state : nullptr);
			startRunner();
		}
		suspendProcessing(false);
	}

	// ____________________________________________________________________________________________
	// The engine's life. m_lifecycle is held by the caller.

	void Processor::createEngine(const juce::MemoryBlock* _state)
	{
		m_runner.reset();
		m_engine.reset();
		m_keeper.reset();
		if(m_rom.empty())
			return;
		auto engine = std::make_unique<g1app::Engine>(m_rom, m_os);
		m_keeper = std::make_unique<g1app::SlotKeeper>();
		m_synthSettings.reset();
		m_pendingSettings.reset();
		m_settingsTries = 0;
		m_settingsWrittenAt = 0;
		m_systemMenu = false;
		m_restoring = false;
		m_presets.reset();
		m_unstarted = false;
		if(_state)
			applyState(*engine, *_state);
		else
		{
			m_keepProjectState = false;
			m_unstarted = true;
			startFromStandalone(*engine);
		}
		m_engine = std::move(engine);
		m_engineFresh = true;
		++m_generation;
		knobsFromEngine();
	}

	// A new engine: its knobs are where its state put them, and the parameters take those
	// positions, without it counting as a gesture. Done here and not in the timer, so a host that
	// moves a knob right after (with no message loop running in between) is not overwritten.
	void Processor::knobsFromEngine()
	{
		std::lock_guard<std::mutex> lock(m_knobMutex);
		auto& mc = m_engine->mc();
		for(size_t k = 0; k < 18; ++k)
		{
			const auto adc = mc.adc(g1::KnobMap::KnobAdc[k]);
			m_lastAdc[k] = adc;
			m_knobParams[k]->setValueNotifyingHost(KnobParameter::toParam(adc));
			m_lastParam[k] = m_knobParams[k]->get();
		}
		m_lastVolumeAdc = mc.adc(g1::g_adcVolume);
		m_volumeParam->setNotifyingHost(VolumeParameter::fromAdc(m_lastVolumeAdc));
		m_lastVolumeParam = m_volumeParam->get();
		m_knobGeneration = m_generation.load();
	}

	// A new instance's banks: a copy of the standalone's flash, or the factory's.
	void Processor::startFromStandalone(g1app::Engine& _engine)
	{
		const juce::File flash(g1app::defaultFlashPath());
		juce::MemoryBlock data;
		if(flash.existsAsFile() && flash.loadFileAsData(data) && _engine.loadFlash(toBytes(data)))
			m_origin = "a copy of the standalone's flash (" + flash.getFullPathName().toStdString() + ")";
		else
			m_origin = "the factory flash: no patches yet";
	}

	void Processor::startRunner()
	{
		m_runner.reset();
		if(!m_engine || !m_prepared)
			return;
		m_runner = std::make_unique<g1app::Runner>(*m_engine, m_rate, static_cast<size_t>(m_maxBlock), m_gainDb,
			m_pcPort && m_pcPort->virtualPorts() ? m_pcPort.get() : nullptr, m_pcIndex, m_keeper.get(),
			&m_synthSettings, m_link.listening() ? &m_link : nullptr, &m_presets);
		setLatencySamples(static_cast<int>(m_runner->latency()));
		m_unstarted = false;
		if(std::exchange(m_engineFresh, false))
			replayPrograms();
	}

	// Queued before the host's first block, half a second into the G1's life: the OS takes them
	// even earlier (its MIDI IN holds the bytes until it reads them), this is margin.
	void Processor::replayPrograms()
	{
		const auto at = static_cast<uint32_t>(m_rate * 0.5);
		for(uint8_t ch = 0; ch < 16; ++ch)
		{
			const auto& p = m_programs[ch];
			if(p.program < 0)
				continue;
			for(const auto [cc, value] : {std::pair{uint8_t(0), p.bankMsb.load()}, std::pair{uint8_t(32), p.bankLsb.load()}})
				if(value >= 0)
				{
					const uint8_t msg[3] = {static_cast<uint8_t>(0xb0 | ch), cc, static_cast<uint8_t>(value)};
					m_runner->queueMidi(at, msg, 3);
				}
			const uint8_t msg[2] = {static_cast<uint8_t>(0xc0 | ch), static_cast<uint8_t>(p.program.load())};
			m_runner->queueMidi(at, msg, 2);
		}
	}

	// Any thread (the host's program parameter): processBlock is the one that queues MIDI.
	void Processor::setCurrentProgram(const int _index)
	{
		if(_index < 0 || _index > 127)
			return;
		m_programs[0].program.store(_index, std::memory_order_relaxed);
		m_hostProgram.store(_index, std::memory_order_release);
	}

	// "channel:msb:lsb:program" for each channel that had a Program Change, separated by spaces.
	std::string Processor::programsToString() const
	{
		std::string out;
		for(int ch = 0; ch < 16; ++ch)
		{
			const auto& p = m_programs[static_cast<size_t>(ch)];
			if(p.program >= 0)
				out += std::to_string(ch) + ":" + std::to_string(p.bankMsb.load()) + ":" + std::to_string(p.bankLsb.load()) + ":" + std::to_string(p.program.load()) + " ";
		}
		return out;
	}

	void Processor::programsFromString(const juce::String& _text)
	{
		for(auto& p : m_programs)
			p.bankMsb = p.bankLsb = p.program = -1;
		for(const auto& item : juce::StringArray::fromTokens(_text, " ", {}))
		{
			const auto f = juce::StringArray::fromTokens(item, ":", {});
			if(f.size() != 4 || f[0].getIntValue() < 0 || f[0].getIntValue() > 15)
				continue;
			auto& p = m_programs[static_cast<size_t>(f[0].getIntValue())];
			p.bankMsb = juce::jlimit(-1, 127, f[1].getIntValue());
			p.bankLsb = juce::jlimit(-1, 127, f[2].getIntValue());
			p.program = juce::jlimit(-1, 127, f[3].getIntValue());
		}
	}

	// ____________________________________________________________________________________________
	// Audio

	void Processor::prepareToPlay(const double _rate, const int _maxBlock)
	{
		std::lock_guard<std::mutex> lock(m_lifecycle);
		m_rate = _rate;
		m_maxBlock = std::max(_maxBlock, 1);
		m_prepared = true;
		m_inputs.setSize(2, m_maxBlock);
		if(!m_engine)
			createEngine(m_haveState ? &m_state : nullptr);
		startRunner();
	}

	void Processor::releaseResources()
	{
		std::lock_guard<std::mutex> lock(m_lifecycle);
		m_runner.reset();
		m_prepared = false;
	}

	bool Processor::isBusesLayoutSupported(const BusesLayout& _layouts) const
	{
		if(_layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
			return false;
		for(int i = 1; i < _layouts.outputBuses.size(); ++i)
			if(!_layouts.outputBuses[i].isDisabled() && _layouts.outputBuses[i] != (i < g_stereoBuses ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono()))
				return false;
		for(const auto& in : _layouts.inputBuses)
			if(!in.isDisabled() && in != juce::AudioChannelSet::stereo())
				return false;
		return true;
	}

	// The runner is only swapped while the host is not in here: in prepareToPlay/releaseResources,
	// or with processing suspended.
	void Processor::processBlock(juce::AudioBuffer<float>& _buffer, juce::MidiBuffer& _midi)
	{
		juce::ScopedNoDenormals noDenormals;
		auto* runner = m_runner.get();
		const auto frames = _buffer.getNumSamples();
		if(!runner || frames <= 0)
		{
			_buffer.clear();
			return;
		}

		// Inputs and outputs share the buffer's channels: the inputs are copied out first.
		const float* ins[2] = {};
		size_t numIns = 0;
		if(auto* bus = getBus(true, 0); bus && bus->isEnabled() && frames <= m_inputs.getNumSamples())
		{
			const auto in = getBusBuffer(_buffer, true, 0);
			for(int c = 0; c < 2 && c < in.getNumChannels(); ++c)
			{
				m_inputs.copyFrom(c, 0, in, c, 0, frames);
				ins[c] = m_inputs.getReadPointer(c);
				numIns = static_cast<size_t>(c) + 1;
			}
		}

		if(const int program = m_hostProgram.exchange(-1, std::memory_order_acq_rel); program >= 0)
		{
			const uint8_t msg[2] = {0xc0, static_cast<uint8_t>(program)};
			runner->queueMidi(0, msg, 2);
		}
		// The host's transport as MIDI clock (issue #20): a VST3 host sends a plugin none, so it
		// is made here, and goes in with the track's MIDI in frame order (the runner's queue is
		// first in, first out). While the transport gives one, the track's own clock, start, stop
		// and song position are left out, so the G1 never gets two clocks.
		bool transport = false;
		if(auto* head = getPlayHead())
			if(const auto pos = head->getPosition())
				if(const auto bpm = pos->getBpm(), ppq = pos->getPpqPosition(); bpm && ppq)
				{
					transport = true;
					m_hostClock.process(pos->getIsPlaying(), *bpm, *ppq, static_cast<uint32_t>(frames), m_rate, m_clockBlock);
				}
		if(!transport)
			m_hostClock.process(false, 0, 0, static_cast<uint32_t>(frames), m_rate, m_clockBlock);	// a Stop if it was playing
		size_t clockAt = 0;
		const auto clockUpTo = [&](const uint32_t _offset)
		{
			for(; clockAt < m_clockBlock.count && m_clockBlock.events[clockAt].offset <= _offset; ++clockAt)
				runner->queueMidi(m_clockBlock.events[clockAt].offset, m_clockBlock.events[clockAt].bytes, m_clockBlock.events[clockAt].size);
		};

		// A project's state going back in is moved on here too (restoreStep), never waiting for the
		// lock: the timer has it, or the message thread is busy, and the next block tries again.
		if(m_restoring.load(std::memory_order_relaxed))
			if(std::unique_lock<std::mutex> lifecycle(m_lifecycle, std::try_to_lock); lifecycle)
				restoreStep();
		const bool restoring = m_restoring.load(std::memory_order_relaxed);
		for(const auto m : _midi)
		{
			if(transport && m.numBytes > 0 && (m.data[0] == 0xf8 || m.data[0] == 0xfa || m.data[0] == 0xfb || m.data[0] == 0xfc || m.data[0] == 0xf2))
				continue;
			if(restoring && m.numBytes > 0 && ((m.data[0] & 0xf0) == 0x80 || (m.data[0] & 0xf0) == 0x90))
				continue;	// no notes while a project's state goes back in (m_restoring)
			clockUpTo(static_cast<uint32_t>(std::max(m.samplePosition, 0)));
			runner->queueMidi(static_cast<uint32_t>(m.samplePosition), m.data, static_cast<size_t>(m.numBytes));
			const auto status = m.data[0] & 0xf0;
			auto& p = m_programs[static_cast<size_t>(m.data[0] & 0x0f)];
			if(status == 0xc0 && m.numBytes >= 2)
				p.program.store(m.data[1], std::memory_order_relaxed);
			else if(status == 0xb0 && m.numBytes >= 3 && (m.data[1] == 0 || m.data[1] == 32))
				(m.data[1] == 0 ? p.bankMsb : p.bankLsb).store(m.data[2], std::memory_order_relaxed);
		}
		clockUpTo(static_cast<uint32_t>(frames));

		// Each output goes to its pair's stereo bus and to its own mono bus, whichever are on: the
		// G1 writes into the first, and the other gets a copy.
		float* outs[4] = {};
		float* copies[4] = {};
		const auto take = [&](const int _c, float* _p) { (outs[_c] ? copies[_c] : outs[_c]) = _p; };
		for(int b = 0; b < getBusCount(false); ++b)
		{
			auto* bus = getBus(false, b);
			if(!bus || !bus->isEnabled())
				continue;
			auto out = getBusBuffer(_buffer, false, b);
			if(b < g_stereoBuses)
				for(int c = 0; c < 2 && c < out.getNumChannels(); ++c)
					take(b * 2 + c, out.getWritePointer(c));
			else if(out.getNumChannels() > 0)
				take(b - g_stereoBuses, out.getWritePointer(0));
		}
		knobsFromHost();
		runner->process(outs, 4, ins, numIns, static_cast<size_t>(frames), isNonRealtime());
		for(int c = 0; c < 4; ++c)
			if(copies[c] && outs[c])
				std::copy_n(outs[c], frames, copies[c]);

		// Channels that are an input and no output of ours.
		for(int c = getTotalNumOutputChannels(); c < _buffer.getNumChannels(); ++c)
			_buffer.clear(c, 0, frames);
	}

	// ____________________________________________________________________________________________
	// State: the G1's user state (the flash against the factory one), the knob positions, and the
	// panel's two preferences. Never the OS: Engine::userState.

	// Asked for a state before there is a G1 (the host saves before it starts the audio): what the
	// plugin has besides the flash, which applyState takes as a new instance's.
	juce::XmlElement Processor::stateXml() const
	{
		juce::XmlElement xml(g_stateTag);
		xml.setAttribute("version", g_stateVersion);
		xml.setAttribute("extrasOpen", m_extrasOpen);
		xml.setAttribute("knobDisplays", m_knobDisplays);
		xml.setAttribute("panelScale", static_cast<double>(m_panelScale));
		xml.setAttribute("knobFollowsPatch", m_knobFollowsPatch);
		xml.setAttribute("randomExclude", juce::String(g1app::knobListToString(m_randomExclude)));
		xml.setAttribute("programs", juce::String(programsToString()));
		return xml;
	}

	void Processor::readPreferences(const juce::XmlElement& _xml)
	{
		m_extrasOpen = _xml.getBoolAttribute("extrasOpen", m_extrasOpen);
		m_knobDisplays = _xml.getBoolAttribute("knobDisplays", m_knobDisplays);
		m_panelScale = static_cast<float>(_xml.getDoubleAttribute("panelScale", m_panelScale));
		m_knobFollowsPatch = _xml.getBoolAttribute("knobFollowsPatch", m_knobFollowsPatch);
		if(_xml.hasAttribute("randomExclude"))
			m_randomExclude = g1app::knobListFromString(_xml.getStringAttribute("randomExclude").toStdString());
	}

	juce::MemoryBlock Processor::settingsOnlyState()
	{
		auto xml = stateXml();
		// The knobs, as far as the host has turned them: their positions, 1..254.
		juce::StringArray knobs;
		for(auto* p : m_knobParams)
			knobs.add(juce::String(KnobParameter::toAdc(p->get())));
		xml.setAttribute("knobs", knobs.joinIntoString(" "));
		xml.setAttribute("volume", VolumeParameter::toAdc(m_volumeParam->get()));
		juce::MemoryBlock out;
		copyXmlToBinary(xml, out);
		return out;
	}

	juce::MemoryBlock Processor::snapshotState()
	{
		knobsFromHost();	// what the host turned while no audio ran (CLAP's flush) goes in too
		std::vector<uint8_t> flash, knobs(256);
		{
			std::unique_lock<std::mutex> engineLock;
			if(m_runner)
				engineLock = m_runner->lockEngine();
			flash = m_engine->userState();
			for(size_t i = 0; i < knobs.size(); ++i)
				knobs[i] = m_engine->mc().adc(static_cast<uint8_t>(i));
		}
		auto xml = stateXml();
		xml.createNewChildElement("Flash")->addTextElement(packBytes(flash));
		xml.createNewChildElement("Knobs")->addTextElement(packBytes(knobs));
		// What each slot holds, which the flash does not (issue #25): as the keeper last read it.
		if(m_keeper)
			xml.createNewChildElement("Slots")->addTextElement(packBytes(g1app::SlotKeeper::pack(m_keeper->slots())));
		// The synth settings as the OS last said them (read every PollMs), however they were changed:
		// the page, the panel's System menu or an editor. The OS keeps them only until it restarts
		// unless stored with Shift + Store (#46). While the project's own are still to be written
		// back, those.
		g1app::SynthSettings settings;
		uint64_t revision = 0;
		if(m_pendingSettings || m_synthSettings.settings(settings, revision))
			xml.createNewChildElement("SynthSettings")->addTextElement(packBytes((m_pendingSettings ? *m_pendingSettings : settings).encode()));
		juce::MemoryBlock out;
		copyXmlToBinary(xml, out);
		return out;
	}

	bool Processor::applyState(g1app::Engine& _engine, const juce::MemoryBlock& _state)
	{
		const auto xml = getXmlFromBinary(_state.getData(), static_cast<int>(_state.getSize()));
		m_keepProjectState = false;
		if(!xml || !xml->hasTagName(g_stateTag))
		{
			m_origin = "the factory flash: the project's state is not a G1-Emu one";
			return false;
		}
		readPreferences(*xml);
		programsFromString(xml->getStringAttribute("programs"));

		std::vector<uint8_t> flash;
		std::string error = "unreadable";
		const auto* flashXml = xml->getChildByName("Flash");
		if(!flashXml)
		{
			// Saved before the G1 ever ran (settingsOnlyState): it starts as a new instance does,
			// with the knobs where the host had turned them.
			startFromStandalone(_engine);
			m_unstarted = true;
			const auto knobs = juce::StringArray::fromTokens(xml->getStringAttribute("knobs"), " ", {});
			for(int k = 0; k < knobs.size() && k < 18; ++k)
				_engine.mc().setAdc(g1::KnobMap::KnobAdc[static_cast<size_t>(k)], static_cast<uint8_t>(juce::jlimit(0, 255, knobs[k].getIntValue())));
			if(xml->hasAttribute("volume"))
				_engine.mc().setAdc(g1::g_adcVolume, static_cast<uint8_t>(juce::jlimit(0, 255, xml->getIntAttribute("volume"))));
			return true;
		}
		bool otherOs = false;
		if( !unpackBytes(flashXml->getAllSubText(), flash) || !_engine.setUserState(flash, error, &otherOs))
		{
			// Kept as it came, and handed back as it came: saving the project again must not
			// replace the user's banks with an empty G1 just because this ROM is not that one.
			m_keepProjectState = true;
			m_origin = "the factory flash: the project's G1 state was not used (" + error + "); it is kept in the project untouched";
			return false;
		}
		std::vector<uint8_t> knobs;
		if(const auto* knobsXml = xml->getChildByName("Knobs"); knobsXml && unpackBytes(knobsXml->getAllSubText(), knobs))
			for(size_t i = 0; i < knobs.size() && i < 256; ++i)
				_engine.mc().setAdc(static_cast<uint8_t>(i), knobs[i]);
		// The slots go back once the G1 is up (after the Program Changes, which they override).
		// A project saved before the keeper existed has none: the keeper reads what there is.
		std::vector<uint8_t> slotBytes;
		g1app::SlotKeeper::Slots slots;
		if(const auto* slotsXml = xml->getChildByName("Slots"); m_keeper && slotsXml
			&& unpackBytes(slotsXml->getAllSubText(), slotBytes) && g1app::SlotKeeper::unpack(slotBytes, slots))
			m_keeper->restore(slots);
		// The project's synth settings win over those stored in the flash: written once the keeper
		// has put the slots back (timerCallback).
		std::vector<uint8_t> settingsBytes;
		g1app::SynthSettings settings;
		if(const auto* settingsXml = xml->getChildByName("SynthSettings"); settingsXml
			&& unpackBytes(settingsXml->getAllSubText(), settingsBytes) && g1app::SynthSettings::decode(settingsBytes, settings))
			m_pendingSettings = settings;
		if(m_keeper && (xml->getChildByName("Slots") || m_pendingSettings))
		{
			m_restoring = true;
			m_restoreStart = juce::Time::getMillisecondCounter();
		}
		m_origin = otherOs ? "this project, saved with another OS: its banks and settings, with the OS in use" : "this project";
		return true;
	}

	void Processor::getStateInformation(juce::MemoryBlock& _dest)
	{
		std::lock_guard<std::mutex> lock(m_lifecycle);
		if(!m_engine || m_keepProjectState)
		{
			_dest = m_haveState ? m_state : settingsOnlyState();
			return;
		}
		// A G1 that has not run yet has nothing of its own: its banks are the standalone's copy
		// it started from, so it is saved as it was loaded, settings and knobs only.
		if(m_unstarted)
		{
			_dest = settingsOnlyState();
			return;
		}
		_dest = snapshotState();
	}

	void Processor::restart()
	{
		juce::MemoryBlock state;
		getStateInformation(state);
		setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	}

	void Processor::setStateInformation(const void* _data, const int _size)
	{
		juce::MemoryBlock state(_data, static_cast<size_t>(std::max(_size, 0)));
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			m_pending = std::move(state);
		}
		// The engine and the editor's panel are replaced on the message thread only.
		if(juce::MessageManager::getInstanceWithoutCreating() && juce::MessageManager::getInstance()->isThisTheMessageThread())
		{
			cancelPendingUpdate();
			handleAsyncUpdate();
		}
		else
			triggerAsyncUpdate();
	}

	void Processor::handleAsyncUpdate()
	{
		juce::MemoryBlock state;
		{
			std::lock_guard<std::mutex> lock(m_pendingMutex);
			state = std::move(m_pending);
			m_pending.reset();
		}
		if(state.isEmpty())
			return;

		if(auto* editor = dynamic_cast<Editor*>(getActiveEditor()))
			editor->engineGoing();
		suspendProcessing(true);
		{
			std::lock_guard<std::mutex> lock(m_lifecycle);
			m_state = std::move(state);
			m_haveState = true;
			if(m_rom.empty())
			{
				// Still read for the panel's preferences, and kept whole for getStateInformation.
				if(const auto xml = getXmlFromBinary(m_state.getData(), static_cast<int>(m_state.getSize())))
					readPreferences(*xml);
			}
			else
			{
				createEngine(&m_state);
				startRunner();
			}
		}
		suspendProcessing(false);
		// The knobs took the project's positions: the host reads the parameters again, as after
		// loading a preset (CLAP asks for that, or it takes them for its own changes).
		updateHostDisplay(ChangeDetails().withProgramChanged(true));
	}

	// Audio thread: a parameter the host moved turns its knob. A position goes to the G1 only when
	// the parameter really changed, so loading a project or a patch never turns a knob by itself.
	void Processor::knobsFromHost()
	{
		std::unique_lock<std::mutex> lock(m_knobMutex, std::try_to_lock);
		if(!lock || !m_engine || m_knobGeneration != m_generation.load())
			return;
		auto& mc = m_engine->mc();
		for(size_t k = 0; k < 18; ++k)
		{
			if(m_toHost[k].load(std::memory_order_acquire))
				continue;	// the panel's value is on its way to the host
			const float v = m_knobParams[k]->get();
			if(v == m_lastParam[k])
				continue;
			m_lastParam[k] = v;
			const auto adc = KnobParameter::toAdc(v);
			mc.setAdc(g1::KnobMap::KnobAdc[k], adc);
			m_lastAdc[k] = adc;
		}
		if(const int v = m_volumeParam->get(); v != m_lastVolumeParam && !m_toHost[VolumeIndex].load(std::memory_order_acquire))
		{
			m_lastVolumeParam = v;
			m_lastVolumeAdc = VolumeParameter::toAdc(v);
			mc.setAdc(g1::g_adcVolume, static_cast<uint8_t>(m_lastVolumeAdc));
		}
	}

	// Message thread: knobs turned by anything but the host go to the host, and the parameters'
	// names follow what the knobs are assigned to.
	void Processor::timerCallback()
	{
		bool renamed = false;
		std::vector<std::pair<size_t, float>> edits;
		{
			std::lock_guard<std::mutex> lifecycle(m_lifecycle);
			if(m_engine)
			{
				auto& mc = m_engine->mc();
				knobsToHost(mc, edits);
				g1::KnobMap map(mc);
				for(uint32_t k = 0; k < 18; ++k)
					renamed = m_knobParams[k]->setInfo(map.read(k)) || renamed;
				const auto now = juce::Time::getMillisecondCounter();
				restoreStep();
				// Now and then, but not while the slots go back in: the OS tells no one of a change on
				// the panel's System menu, of the synth settings or of the active slot's patch settings
				// (voices, bend range...).
				if(!m_restoring && m_runner && m_keeper && now - m_polledAt >= PollMs)
				{
					m_polledAt = now;
					// The project saves the synth settings as the OS last said them (but its own, while
					// they are still to be written).
					if(!m_pendingSettings)
						m_synthSettings.read();
					// The active slot is read again while the System menu is open, and once after.
					const bool systemMenu = !(mc.ledRow(SystemLedRow) & (1u << SystemLedBit));	// active low
					if(systemMenu || m_systemMenu)
						m_keeper->reread(mc.read8(g1::KnobMap::ActiveSlot + map.osShift()) & 3);
					m_systemMenu = systemMenu;
				}
			}
		}
		// Outside the locks: the host may call back into the plugin from inside these calls (to
		// learn the parameter, to keep an undo step, to ask for the new names).
		tellHost(edits);
		if(renamed)
			updateHostDisplay(ChangeDetails().withParameterInfoChanged(true));
	}

	// A project's state going back in, a step further: the synth settings written once the slots are
	// back, and the notes let in once they are too. From the timer, and from processBlock as well:
	// a host that renders a project offline right after opening it may run no message loop
	// meanwhile, and the notes would stay held.
	void Processor::restoreStep()
	{
		if(m_pendingSettings && m_keeper)
			writePendingSettings();
		if(m_restoring && ((!m_pendingSettings && m_keeper && m_keeper->settled())
			|| juce::Time::getMillisecondCounter() - m_restoreStart > RestoreHoldMs))
			m_restoring = false;
	}

	// The project's synth settings, once the keeper has put the slots back: written, then compared
	// with the OS's reading back (or, if none comes, SettingsCheckMs later); written again if the OS
	// did not take them.
	void Processor::writePendingSettings()
	{
		const auto now = juce::Time::getMillisecondCounter();
		if(m_settingsWrittenAt == 0)
		{
			if(m_settingsTries >= SettingsTries || !m_keeper->settled())
				return;
			g1app::SynthSettings os;
			m_synthSettings.settings(os, m_settingsRevision);
			m_synthSettings.write(*m_pendingSettings);
			m_settingsWrittenAt = std::max<juce::uint32>(now, 1);
			++m_settingsTries;
			return;
		}
		// The write is read back: decided as soon as that reading comes, or at SettingsCheckMs.
		g1app::SynthSettings os;
		uint64_t rev = 0;
		const bool readBack = m_synthSettings.settings(os, rev) && rev > m_settingsRevision;
		if(!readBack && now - m_settingsWrittenAt < SettingsCheckMs)
			return;
		const bool took = readBack && os == *m_pendingSettings;
		if(took || m_settingsTries >= SettingsTries)
		{
			// Taken, the project saves them from the OS's readings from now on. Given up, they stay
			// pending, not written again, so the project still saves its own and not the OS's.
			if(took)
			{
				m_pendingSettings.reset();
				m_settingsTries = 0;
			}
			m_restoring = false;	// the slots were back before the write: the notes can come in
		}
		m_settingsWrittenAt = 0;
	}

	// Each knob that something other than the host turned, with the value its parameter takes.
	void Processor::knobsToHost(g1::Microcontroller& _mc, std::vector<std::pair<size_t, float>>& _edits)
	{
		std::lock_guard<std::mutex> lock(m_knobMutex);
		for(size_t k = 0; k < 18; ++k)
		{
			const int adc = _mc.adc(g1::KnobMap::KnobAdc[k]);
			if(adc == m_lastAdc[k])
				continue;
			m_lastAdc[k] = adc;
			const float v = KnobParameter::toParam(static_cast<uint8_t>(adc));
			m_lastParam[k] = m_knobParams[k]->convertFrom0to1(m_knobParams[k]->convertTo0to1(v));	// as the parameter will hold it
			m_toHost[k].store(true, std::memory_order_release);
			_edits.emplace_back(k, v);
		}
		// The volume's parameter moves by whole steps: a position that keeps the step tells nothing.
		const int adc = _mc.adc(g1::g_adcVolume);
		if(adc == m_lastVolumeAdc)
			return;
		m_lastVolumeAdc = adc;
		if(const int v = VolumeParameter::fromAdc(adc); v != m_volumeParam->get())
		{
			m_lastVolumeParam = v;
			m_toHost[VolumeIndex].store(true, std::memory_order_release);
			_edits.emplace_back(VolumeIndex, m_volumeParam->convertTo0to1(static_cast<float>(v)));
		}
	}

	// Message thread, no lock held: the edits as gestures, one per turn (HostGestures), and the
	// turns that have stopped, ended.
	void Processor::tellHost(const std::vector<std::pair<size_t, float>>& _edits)
	{
		const auto now = juce::Time::getMillisecondCounter();
		std::vector<HostGestures<19>::Event> events;
		for(const auto& [index, value] : _edits)
			m_gestures.changed(index, value, now, events);
		m_gestures.idle(now, events);
		for(const auto& e : events)
		{
			juce::RangedAudioParameter& p = e.index == VolumeIndex ? static_cast<juce::RangedAudioParameter&>(*m_volumeParam)
				: static_cast<juce::RangedAudioParameter&>(*m_knobParams[e.index]);
			switch(e.kind)
			{
			case HostGestures<19>::Kind::Begin:	p.beginChangeGesture(); break;
			case HostGestures<19>::Kind::Value:
				p.setValueNotifyingHost(e.value);
				m_toHost[e.index].store(false, std::memory_order_release);
				break;
			case HostGestures<19>::Kind::End:	p.endChangeGesture(); break;
			}
		}
	}

	// ____________________________________________________________________________________________
	// For the editor

	g1app::Engine* Processor::engine()
	{
		std::lock_guard<std::mutex> lock(m_lifecycle);
		return m_engine.get();
	}

	g1app::HostStats Processor::stats()
	{
		std::lock_guard<std::mutex> lock(m_lifecycle);
		if(m_runner)
			return m_runner->stats();
		g1app::HostStats s;
		s.audio = m_engine ? "waiting for the host to start the audio" : "no ROM";
		s.midi = "the DAW track";
		return s;
	}

	std::string Processor::describe()
	{
		std::lock_guard<std::mutex> lock(m_lifecycle);
		std::string d = "ROM: " + (m_romPath.empty() ? std::string("none") : m_romPath) + "\n";
		if(m_runner)
		{
			char buf[160];
			std::snprintf(buf, sizeof(buf), "Latency: %zu frames (%.1f ms at %.0f Hz), reported to the host\n",
				m_runner->latency(), 1000.0 * static_cast<double>(m_runner->latency()) / m_runner->rate(), m_runner->rate());
			d += buf;
		}
		if(m_engine)
			d += "This instance's patches and synth settings came from " + m_origin + ".\n"
				 "They are saved in the DAW project, never in the standalone's flash, and so is the last\n"
				 "Program Change of each channel, sent again when the project opens (the G1 does not\n"
				 "remember what each slot had).\n";
		d += "Notes and controllers come from the track.\n";
		if(!m_pcPort)
			d += "PC Port: " + m_pcProblem + ".";
		else if(m_pcPort->virtualPorts())
			d += "PC Port: the MIDI port \"" + pcPortClient(m_instance)
				+ " PC Port\". Choose it in Animatek NME as input and output to edit this instance.";
		else
			d += "PC Port: " + m_pcPort->describe() + ".";
		d += "\nDirect link: ";
		if(m_link.listening())
			d += "\"" + m_link.name() + "\" on port " + std::to_string(m_link.port())
				+ ". Animatek NME finds it by itself, with no MIDI port.";
		else
			d += m_linkProblem + ".";
		return d;
	}

	juce::AudioProcessorEditor* Processor::createEditor()
	{
		return new Editor(*this);
	}
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
	return new g1plugin::Processor();
}
