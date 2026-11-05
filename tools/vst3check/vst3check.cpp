// g1vst3check: loads the built G1-Emu.vst3 through JUCE's VST3 host, as a DAW would, and plays it.
//
//   g1vst3check path/to/G1-Emu.vst3 [--slots | --settings | --window | --buffer-change | --clock packets-dir | --split-upload packets-dir]
//   g1vst3check "path/to/G1-Emu FX.vst3" --fx packets-dir
//
// Two instances at once in one process, each with its own Program Change and a note, rendered
// offline block by block and interleaved; then the first one's state goes into a third instance,
// which must come back with the same reported latency and play its patch without being told; and the editor is opened and
// closed when there is a display. While A and B play, each one's PC Port gets the editor's greeting
// (IAm) through its virtual MIDI port, as Animatek NME sends it, and must answer on its own port and
// not on the other's (Linux and macOS: on Windows JUCE makes no virtual ports). Needs a ROM where the plugin looks for one: without it, 77.
// With every bus on, the mono outputs Out 1..4 must carry exactly what the stereo pairs carry (#27).
//
// --clock, alone (issue #20): the host's transport clocks the G1 (checkClock).
//
// --slots, alone (issue #25, where there are PC Ports): a patch sent to slot A through the PC
// Port, as an editor sends it and not stored in any bank, must be in the project saved
// afterwards; a new instance opened from that project, with no editor, must play it.
//
// --settings, alone (issue #46): the synth settings in use are in the project, and a project's
// own (slot A put on channel 5) are the ones a new instance opened from it runs with.
//
// --fx dir, alone (issue #76), on G1-Emu FX: the host must see an effect with its input on from
// the start, and a sine into that input must come out once AudioInTest.pch (AudioIn to outputs 1
// and 2; the packets as g1patchtest --dump-packets writes them, in dir) is in slot A, and stop
// with it. The patch goes over the direct link (TCP, as NME reaches the plugin), not the virtual
// PC Port, so it also runs where those ports are not made.
//
// --buffer-change, alone (issue #66): the buffer size changed as Cubase and Nuendo change it, the
// plugin deactivated and reactivated from inside its own prepareToPlay when its latency changes.

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <pluginterfaces/vst/ivstcomponent.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace
{
	constexpr double g_rate = 48000.0;
	constexpr int g_block = 512;

	struct Voice
	{
		std::unique_ptr<juce::AudioPluginInstance> plugin;
		int program = -1;
		float peakBefore = 0, peakAfter = 0;
	};

	// The direct link's ports already taken before this process loads its plugin: a standalone G1-Emu
	// or a DAW's instance listens on the first ones, and the check talked to that one (a standalone got
	// the check's patches in its slot A, 2026-11-04). The instance loaded here takes the first free one.
	std::set<int> linkPortsTaken()
	{
		std::set<int> taken;
		for(int port = 47310; port < 47318; ++port)
		{
			juce::StreamingSocket s;
			if(s.connect("127.0.0.1", port, 200))
				taken.insert(port);
		}
		return taken;
	}

	// Connects to the direct link of the instance loaded since linkPortsTaken() said _taken.
	bool connectToNewLink(juce::StreamingSocket& _link, const std::set<int>& _taken)
	{
		for(int port = 47310; port < 47318; ++port)
			if(!_taken.count(port) && _link.connect("127.0.0.1", port, 2000))
				return true;
		return false;
	}

	std::unique_ptr<juce::AudioPluginInstance> load(juce::AudioPluginFormatManager& _formats, const juce::String& _path)
	{
		juce::OwnedArray<juce::PluginDescription> found;
		juce::VST3PluginFormat vst3;
		vst3.findAllTypesForFile(found, _path);
		if(found.isEmpty())
			return {};
		juce::String error;
		auto p = _formats.createPluginInstance(*found[0], g_rate, g_block, error);
		if(!p)
			std::printf("cannot instantiate: %s\n", error.toRawUTF8());
		return p;
	}

	// An editor at the other end of the instances' PC Ports: what each port has answered.
	class Editor final : public juce::MidiInputCallback
	{
	public:
		bool open(const juce::String& _port)
		{
			std::unique_ptr<juce::MidiOutput> out;
			std::unique_ptr<juce::MidiInput> in;
			for(const auto& d : juce::MidiOutput::getAvailableDevices())
				if(d.name == _port)
					out = juce::MidiOutput::openDevice(d.identifier);
			for(const auto& d : juce::MidiInput::getAvailableDevices())
				if(d.name == _port)
					in = juce::MidiInput::openDevice(d.identifier, this);
			if(!out || !in)
				return false;
			in->start();
			m_names[in.get()] = _port;
			m_outs[_port] = std::move(out);
			m_ins.push_back(std::move(in));
			return true;
		}
		void greet(const juce::String& _port)
		{
			const uint8_t iAm[] = {0xf0, 0x33, 0x00, 0x06, 0x00, 0x03, 0x03, 0xf7};
			m_outs[_port]->sendMessageNow(juce::MidiMessage(iAm, sizeof(iAm)));
		}
		void send(const juce::String& _port, const std::vector<uint8_t>& _sysex)
		{
			m_outs[_port]->sendMessageNow(juce::MidiMessage(_sysex.data(), static_cast<int>(_sysex.size())));
		}
		void handleIncomingMidiMessage(juce::MidiInput* _source, const juce::MidiMessage& _m) override
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			// Only the answer to IAm (F0 33 00 06 01 03 ...): the G1 also speaks unasked on this
			// port, at boot and at every Program Change.
			const auto* d = _m.getRawData();
			if(_m.getRawDataSize() > 6 && d[0] == 0xf0 && d[1] == 0x33 && d[2] == 0x00 && d[4] == 0x01 && d[5] == 0x03)
				++m_replies[m_names[_source]];
			// An upload packet's ACK (cc $16, $36 or $7F), for the --slots editor.
			if(_m.getRawDataSize() > 5 && d[0] == 0xf0 && d[1] == 0x33 && (d[2] >> 2) == 0x16 && (d[5] == 0x36 || d[5] == 0x7f))
				++m_acks;
		}
		int acks()
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_acks;
		}
		int replies(const juce::String& _port)
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			return m_replies[_port];
		}
	private:
		std::mutex m_mutex;
		std::map<juce::MidiInput*, juce::String> m_names;
		std::map<juce::String, int> m_replies;
		int m_acks = 0;
		std::map<juce::String, std::unique_ptr<juce::MidiOutput>> m_outs;
		std::vector<std::unique_ptr<juce::MidiInput>> m_ins;
	};

	// Plays seconds of audio on every voice, block by block and in turn. A Program Change at
	// 2 s (if the voice has one), the note at 3 s, released at 4.5 s. _onBlock, if given, is
	// called before each round of blocks with its first frame.
	void play(std::vector<Voice*> _voices, const double _seconds, const std::function<void(int)>& _onBlock = {})
	{
		const auto total = static_cast<int>(_seconds * g_rate);
		const int pcAt = static_cast<int>(2.0 * g_rate), onAt = static_cast<int>(3.0 * g_rate), offAt = static_cast<int>(4.5 * g_rate);
		for(auto* v : _voices)
		{
			v->plugin->setNonRealtime(true);
			v->plugin->prepareToPlay(g_rate, g_block);
		}
		for(int pos = 0; pos < total; pos += g_block)
		{
			if(_onBlock)
				_onBlock(pos);
			for(auto* v : _voices)
			{
				auto& p = *v->plugin;
				juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), g_block);
				buffer.clear();
				juce::MidiBuffer midi;
				auto at = [&](const int _frame, const juce::MidiMessage& _m)
				{
					if(_frame >= pos && _frame < pos + g_block)
						midi.addEvent(_m, _frame - pos);
				};
				if(v->program >= 0)
					at(pcAt, juce::MidiMessage::programChange(1, v->program));
				at(onAt, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)));
				at(offAt, juce::MidiMessage::noteOff(1, 60));
				p.processBlock(buffer, midi);
				float peak = 0;
				for(int c = 0; c < p.getTotalNumOutputChannels(); ++c)
					peak = std::max(peak, buffer.getMagnitude(c, 0, g_block));
				(pos < onAt ? v->peakBefore : v->peakAfter) = std::max(pos < onAt ? v->peakBefore : v->peakAfter, peak);
			}
		}
		for(auto* v : _voices)
			v->plugin->releaseResources();
	}

	float db(const float _v) { return _v > 0 ? 20.0f * std::log10(_v) : -200.0f; }

	// SimpleOSC (OscA into the 2-Output, it drones with no note) as NME uploads it to slot A: the
	// sections back to back, in packets of 32 bytes, each 7-bit packed and framed.
	std::vector<std::vector<uint8_t>> simpleOscUpload()
	{
		const std::vector<std::vector<uint8_t>> sections = {
			{0x37, 0x00, 0x00, 0x00, 0x53, 0x69, 0x6d, 0x70, 0x6c, 0x65, 0x4f, 0x53, 0x43, 0x00},
			{0x21, 0x01, 0xfc, 0x07, 0xf1, 0x00, 0x40, 0x7d, 0x02, 0xfe, 0x78},
			{0x4a, 0x82, 0x0e, 0x04, 0x10, 0x30, 0x80, 0x81, 0x09}, {0x4a, 0x00},
			{0x69, 0x80, 0x00, 0x00, 0x20, 0x00, 0x00},
			{0x52, 0x80, 0x02, 0x00, 0x40, 0x82, 0x00, 0x01, 0x02, 0x08, 0x10}, {0x52, 0x00, 0x00},
			{0x4d, 0x82, 0x02, 0x1e, 0x04, 0x08, 0x10, 0x00, 0x00, 0x00, 0x00, 0x02, 0x09, 0x90, 0x00}, {0x4d, 0x00},
			{0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, {0x62, 0xc0, 0x40, 0x60, 0x20, 0x40, 0x00, 0x00}, {0x60, 0x00},
			{0x5b, 0x80}, {0x5b, 0x00},
			{0x5a, 0x82, 0x01, 0x4f, 0x73, 0x63, 0x41, 0x00, 0x02, 0x32, 0x4f, 0x75, 0x74, 0x70, 0x75, 0x74, 0x00}, {0x5a, 0x00}};
		std::vector<std::pair<std::vector<uint8_t>, int>> packets(1);
		for(const auto& sec : sections)
		{
			for(const auto b : sec)
			{
				if(packets.back().first.size() == 32)
					packets.emplace_back();
				packets.back().first.push_back(b);
			}
			++packets.back().second;
		}
		std::vector<std::vector<uint8_t>> out;
		for(size_t i = 0; i < packets.size(); ++i)
		{
			const int cc = 0x1c | (i == 0 ? 1 : 0) | (i + 1 == packets.size() ? 2 : 0);
			std::vector<uint8_t> m = {0xf0, 0x33, static_cast<uint8_t>(cc << 2), 0x06, static_cast<uint8_t>(0x40 | packets[i].second)};
			uint32_t buffer = 0;
			int held = 0;
			for(const auto b : packets[i].first)
			{
				buffer = (buffer << 8) | b;
				held += 8;
				while(held >= 7) { held -= 7; m.push_back(static_cast<uint8_t>((buffer >> held) & 0x7f)); }
			}
			if(held > 0)
				m.push_back(static_cast<uint8_t>((buffer << (7 - held)) & 0x7f));
			uint32_t sum = 0;
			for(const auto b : m) sum += b;
			m.push_back(static_cast<uint8_t>(sum & 0x7f));
			m.push_back(0xf7);
			out.push_back(m);
		}
		return out;
	}

	// Plays _seconds with no MIDI, offline, and returns the peak of each whole second.
	std::vector<float> drone(juce::AudioPluginInstance& _p, const double _seconds, const std::function<void(int)>& _onBlock = {})
	{
		_p.setNonRealtime(true);
		_p.prepareToPlay(g_rate, g_block);
		std::vector<float> peaks(static_cast<size_t>(std::ceil(_seconds)), 0.0f);
		const auto total = static_cast<int>(_seconds * g_rate);
		for(int pos = 0; pos < total; pos += g_block)
		{
			if(_onBlock)
				_onBlock(pos);
			juce::AudioBuffer<float> buffer(std::max(_p.getTotalNumInputChannels(), _p.getTotalNumOutputChannels()), g_block);
			buffer.clear();
			juce::MidiBuffer midi;
			_p.processBlock(buffer, midi);
			auto& peak = peaks[static_cast<size_t>(pos / static_cast<int>(g_rate))];
			for(int c = 0; c < _p.getTotalNumOutputChannels(); ++c)
				peak = std::max(peak, buffer.getMagnitude(c, 0, g_block));
		}
		_p.releaseResources();
		return peaks;
	}

	std::string peaksText(const std::vector<float>& _peaks)
	{
		std::string s;
		for(const auto p : _peaks)
			s += juce::String(db(p), 1).toStdString() + " ";
		return s;
	}

	// Issue #25 (--slots), in a process of its own: within one process JUCE does not make the
	// virtual ports of instances created after others have closed. A patch sent to slot A through
	// the PC Port, as an editor sends it and stored in no bank, must be in the project saved
	// afterwards; a new instance opened from that project, with no editor, must play it.
	int checkSlots(juce::AudioPluginFormatManager& formats, const juce::String& path)
	{
		int failed = 0;

		// A process of its own, so this first instance is the first on the direct link. The editor
		// talks over the direct link, as Animatek NME does: the plugin's virtual MIDI port is made by
		// the JUCE this process shares with it, and JUCE does not list its own client's ports
		// (2026-11-03: "NOT THERE" though the port was open).
		Voice f;
		const auto taken = linkPortsTaken();
		f.plugin = load(formats, path);
		juce::StreamingSocket link;
		const bool open = connectToNewLink(link, taken);
		std::printf("issue #25: editor on the direct link: %s\n", open ? "open" : "NOT THERE");
		std::vector<uint8_t> rx;
		bool greeted = false;
		int acks = 0;
		const auto receive = [&]
		{
			uint8_t buf[512];
			while(open && link.waitUntilReady(true, 0) == 1)
			{
				const int n = link.read(buf, sizeof(buf), false);
				if(n <= 0)
					break;
				rx.insert(rx.end(), buf, buf + n);
			}
			if(!greeted)
				if(const auto nl = std::find(rx.begin(), rx.end(), '\n'); nl != rx.end())
				{
					greeted = true;
					rx.erase(rx.begin(), nl + 1);
				}
			if(!greeted)
				return;
			// An upload packet's ACK (cc $16, $36 or $7F).
			for(size_t i = 0; i + 5 < rx.size(); ++i)
				if(rx[i] == 0xf0 && rx[i + 1] == 0x33 && (rx[i + 2] >> 2) == 0x16 && (rx[i + 5] == 0x36 || rx[i + 5] == 0x7f))
					++acks;
			rx.clear();
		};
		const auto upload = simpleOscUpload();
		// From 6 s, after the G1 has booted and the plugin has read its slots: each packet as soon
		// as the last one's ACK is back, as NME sends them. The render is offline, so each block
		// waits a moment for the bytes to travel.
		size_t sent = 0;
		const auto peaksF = drone(*f.plugin, 11.0, [&](const int _pos)
		{
			receive();
			if(!open || _pos < static_cast<int>(6.0 * g_rate) || sent == upload.size() || acks < static_cast<int>(sent))
				return;
			link.write(upload[sent].data(), static_cast<int>(upload[sent].size()));
			++sent;
			for(int i = 0; i < 50 && acks < static_cast<int>(sent); ++i)
			{
				juce::Thread::sleep(2);
				receive();
			}
		});
		std::printf("issue #25: %zu of %zu packets sent, %d ACKs\n", sent, upload.size(), acks);
		link.close();
		juce::MemoryBlock project;
		f.plugin->getStateInformation(project);
		f.plugin.reset();
		Voice g;
		g.plugin = load(formats, path);
		g.plugin->setStateInformation(project.getData(), static_cast<int>(project.getSize()));
		const auto peaksG = drone(*g.plugin, 6.0);
		std::printf("issue #25: patch sent to slot A over the PC Port; peak per second %s\n", peaksText(peaksF).c_str());
		std::printf("issue #25: project reopened in a new instance, no editor; peak per second %s\n", peaksText(peaksG).c_str());
		const float playing = peaksF.back(), reopened = peaksG.back();
		if(!open || sent != upload.size() || playing < 1e-3f || reopened < 1e-3f || std::abs(db(playing) - db(reopened)) > 1.0f)
		{
			std::printf("FAIL: the patch in slot A does not come back with the project\n");
			failed = 1;
		}
		return failed;
	}

	// Issue #76 (--fx dir), in a process of its own: G1-Emu FX on an audio track. The host must
	// see an effect whose input is on as it comes; AudioInTest.pch goes to slot A over the direct
	// link at 6 s, a sine plays into the input from 9 to 12 s and stops, and outputs 1/2 must carry it
	// then and fall silent after.
	// With add-NN.syx files in dir too (g1patchtest --dump-packets with --add-module-at, issue #85),
	// the sine plays from 8 s to the end and from 10 s one module is dropped into the patch every
	// 0.6 s over the same link, as an editor sends them, while the plugin's own links talk on the PC
	// Port too: the sine must still come through, every second, to the end.
	int checkFx(juce::AudioPluginFormatManager& _formats, const juce::String& _path, const juce::File& _packets)
	{
		int failed = 0;
		std::vector<std::vector<uint8_t>> upload;
		for(const auto& f : _packets.findChildFiles(juce::File::findFiles, false, "packet-*.syx"))
		{
			juce::MemoryBlock data;
			f.loadFileAsData(data);
			upload.emplace_back(static_cast<const uint8_t*>(data.getData()), static_cast<const uint8_t*>(data.getData()) + data.getSize());
		}
		std::vector<std::vector<uint8_t>> adds;
		for(const auto& f : _packets.findChildFiles(juce::File::findFiles, false, "add-*.syx"))
		{
			juce::MemoryBlock data;
			f.loadFileAsData(data);
			adds.emplace_back(static_cast<const uint8_t*>(data.getData()), static_cast<const uint8_t*>(data.getData()) + data.getSize());
		}
		const bool editing = !adds.empty();
		Voice v;
		const auto taken = linkPortsTaken();
		v.plugin = load(_formats, _path);
		if(!v.plugin)
			return 1;
		auto& p = *v.plugin;
		const bool instrument = p.getPluginDescription().isInstrument;
		const auto* in = p.getBus(true, 0);
		const bool inputOn = in && in->isEnabled() && in->getNumberOfChannels() == 2;
		std::printf("issue #76: \"%s\", %s, input %s\n", p.getName().toRawUTF8(), instrument ? "an INSTRUMENT" : "an effect",
			inputOn ? "on, stereo" : "NOT ON");
		if(instrument || !inputOn)
			failed = 1;

		// The first instance listens on the direct link's first port and greets with a line.
		juce::StreamingSocket link;
		std::string greeting;
		const bool open = !upload.empty() && connectToNewLink(link, taken);
		std::vector<uint8_t> rx;
		int acks = 0;
		const auto receive = [&]
		{
			uint8_t buf[512];
			while(open && link.waitUntilReady(true, 0) == 1)
			{
				const int n = link.read(buf, sizeof(buf), false);
				if(n <= 0)
					break;
				rx.insert(rx.end(), buf, buf + n);
			}
			if(greeting.empty())
				if(const auto nl = std::find(rx.begin(), rx.end(), '\n'); nl != rx.end())
				{
					greeting.assign(rx.begin(), nl);
					rx.erase(rx.begin(), nl + 1);
				}
			// An upload packet's ACK (cc $16, $36 or $7F), as the Editor above counts them.
			for(size_t i = 0; i + 5 < rx.size(); ++i)
				if(rx[i] == 0xf0 && rx[i + 1] == 0x33 && (rx[i + 2] >> 2) == 0x16 && (rx[i + 5] == 0x36 || rx[i + 5] == 0x7f))
					++acks;
			if(!greeting.empty())
				rx.clear();
		};

		p.setNonRealtime(true);
		p.prepareToPlay(g_rate, g_block);
		const int total = static_cast<int>((editing ? 10.0 + 0.6 * static_cast<double>(adds.size()) + 4.0 : 14.0) * g_rate);
		float during = 0, after = 0, last = 0;
		size_t sent = 0, added = 0;
		std::vector<float> perSecond(static_cast<size_t>(total / g_rate) + 1, 0.0f);
		for(int pos = 0; pos < total; pos += g_block)
		{
			receive();
			if(open && pos >= static_cast<int>(6.0 * g_rate) && sent < upload.size() && acks >= static_cast<int>(sent))
			{
				link.write(upload[sent].data(), static_cast<int>(upload[sent].size()));
				++sent;
				for(int i = 0; i < 50 && acks < static_cast<int>(sent); ++i)
				{
					juce::Thread::sleep(2);
					receive();
				}
			}
			juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), g_block);
			buffer.clear();
			for(int i = 0; i < g_block; ++i)
			{
				const double t = (pos + i) / g_rate;
				const bool on = editing ? t >= 8.0 : (t >= 9.0 && t < 12.0);
				const auto x = on ? static_cast<float>(0.25 * std::sin(2.0 * juce::MathConstants<double>::pi * 440.0 * t)) : 0.0f;
				for(int c = 0; c < p.getTotalNumInputChannels(); ++c)
					buffer.setSample(c, i, x);
			}
			// The editor's modules, one every 0.6 s from 10 s, once the patch is in.
			if(editing && open && sent == upload.size() && added < adds.size() && pos >= static_cast<int>((10.0 + 0.6 * static_cast<double>(added)) * g_rate))
			{
				link.write(adds[added].data(), static_cast<int>(adds[added].size()));
				++added;
			}
			juce::MidiBuffer midi;
			p.processBlock(buffer, midi);
			const double t = pos / g_rate;
			float peak = 0;
			for(int c = 0; c < 2; ++c)
				peak = std::max(peak, buffer.getMagnitude(c, 0, g_block));
			perSecond[static_cast<size_t>(t)] = std::max(perSecond[static_cast<size_t>(t)], peak);
			if(t >= 10.0 && t < (editing ? 10.0 + 0.6 * static_cast<double>(adds.size()) : 12.0))
				during = std::max(during, peak);
			else if(editing && t >= total / g_rate - 2.0)
				last = std::max(last, peak);
			else if(!editing && t >= 13.0)
				after = std::max(after, peak);
		}
		p.releaseResources();
		std::printf("issue #76: direct link %s (\"%s\")\n", open ? "open" : "NOT THERE", greeting.c_str());
		std::printf("issue #76: %zu of %zu packets sent, %d ACKs; outputs 1/2 with the sine in %.1f dB, after it %.1f dB\n",
			sent, upload.size(), acks, db(during), db(after));
		if(editing)
		{
			// From 9 s, when the sine is through, every second must carry it.
			size_t quiet = 0;
			std::printf("issue #85: %zu of %zu modules added; peak per second", added, adds.size());
			for(size_t i = 0; i < perSecond.size(); ++i)
			{
				std::printf(" %.0f", db(perSecond[i]));
				if(i >= 9 && i + 1 < perSecond.size() && perSecond[i] < 1e-3f)
					++quiet;
			}
			std::printf("\nissue #85: while adding %.1f dB, the last 2 s %.1f dB\n", db(during), db(last));
			if(!open || sent != upload.size() || added != adds.size() || quiet || during < 1e-3f || last < during * 0.5f)
			{
				std::printf("FAIL: the input stopped coming through while modules were added\n");
				failed = 1;
			}
			return failed;
		}
		if(!open || sent != upload.size() || during < 1e-3f || after > during * 0.01f)
		{
			std::printf("FAIL: the input does not reach the G1 through AudioIn\n");
			failed = 1;
		}
		return failed;
	}

	// A host's transport, as JUCE's VST3 host hands it to the plugin (ProcessContext).
	struct Transport final : juce::AudioPlayHead
	{
		double bpm = 120.0, ppq = 0.0;
		bool playing = false;
		juce::Optional<PositionInfo> getPosition() const override
		{
			PositionInfo p;
			p.setBpm(bpm);
			p.setPpqPosition(ppq);
			p.setIsPlaying(playing);
			return p;
		}
	};

	// Issue #20 (--clock dir), in a process of its own: the host's transport must reach the G1 as
	// MIDI clock. ClockTest.pch (MIDIGlobal's clock to output 1; the packets as g1patchtest
	// --dump-packets writes them, in dir) goes to slot A over the PC Port while the transport is
	// stopped; then the transport plays 4 s at 120 BPM and 4 s at 90 BPM, and stops. MIDIGlobal
	// gives 24 pulses a beat when the G1's clock is external, which a new flash's is: 48 Hz, then
	// 36 Hz, then nothing.
	int checkClock(juce::AudioPluginFormatManager& _formats, const juce::String& _path, const juce::File& _packets)
	{
		std::vector<std::vector<uint8_t>> upload;
		for(const auto& f : _packets.findChildFiles(juce::File::findFiles, false, "packet-*.syx"))
		{
			juce::MemoryBlock data;
			f.loadFileAsData(data);
			upload.emplace_back(static_cast<const uint8_t*>(data.getData()), static_cast<const uint8_t*>(data.getData()) + data.getSize());
		}
		Voice v;
		v.plugin = load(_formats, _path);
		auto& p = *v.plugin;
		Transport transport;
		p.setPlayHead(&transport);
		const juce::String port = "G1-Emu PC Port";
		Editor sender;
		const bool open = !upload.empty() && sender.open(port);
		std::printf("issue #20: %zu packets, editor on \"%s\": %s\n", upload.size(), port.toRawUTF8(), open ? "open" : "NOT THERE");

		p.setNonRealtime(true);
		p.prepareToPlay(g_rate, g_block);
		const int total = static_cast<int>(20.0 * g_rate);
		std::vector<float> out(static_cast<size_t>(total), 0.0f);
		size_t sent = 0;
		for(int pos = 0; pos < total; pos += g_block)
		{
			if(open && pos >= static_cast<int>(6.0 * g_rate) && sent < upload.size() && sender.acks() >= static_cast<int>(sent))
			{
				sender.send(port, upload[sent++]);
				for(int i = 0; i < 50 && sender.acks() < static_cast<int>(sent); ++i)
					juce::Thread::sleep(2);
			}
			const double t = pos / g_rate;
			transport.playing = t >= 10.0 && t < 18.0;
			transport.bpm = t < 14.0 ? 120.0 : 90.0;
			juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), g_block);
			buffer.clear();
			juce::MidiBuffer midi;
			p.processBlock(buffer, midi);
			for(int i = 0; i < g_block && pos + i < total; ++i)
				out[static_cast<size_t>(pos + i)] = buffer.getSample(0, i);
			if(transport.playing)
				transport.ppq += transport.bpm / 60.0 / g_rate * g_block;
		}
		p.releaseResources();

		// Pulses a second between _from and _to: rising edges through half the loudest sample.
		const auto rate = [&](const double _from, const double _to)
		{
			const auto a = static_cast<size_t>(_from * g_rate), b = static_cast<size_t>(_to * g_rate);
			float peak = 0;
			for(size_t i = a; i < b; ++i)
				peak = std::max(peak, std::abs(out[i]));
			if(peak < 1e-3f)
				return 0.0;
			int edges = 0;
			for(size_t i = a + 1; i < b; ++i)
				edges += out[i - 1] <= peak * 0.5f && out[i] > peak * 0.5f;
			return edges / (_to - _from);
		};
		const double r120 = rate(11.0, 14.0), r90 = rate(15.0, 18.0), stopped = rate(19.0, 20.0);

		// How late each pulse is against its beat, beyond the latency the plugin reports (which the
		// host makes up for): at 120 BPM, tick k is at 10 s + k * 1000 frames.
		const auto lag = [&]
		{
			const double latency = p.getLatencySamples();
			const double framesPerTick = g_rate * 60.0 / 120.0 / 24.0;
			const auto a = static_cast<size_t>(11.0 * g_rate), b = static_cast<size_t>(14.0 * g_rate);
			float peak = 0;
			for(size_t i = a; i < b; ++i)
				peak = std::max(peak, std::abs(out[i]));
			double sum = 0, lo = 1e9, hi = -1e9;
			int n = 0;
			for(size_t i = a + 1; i < b; ++i)
				if(out[i - 1] <= peak * 0.5f && out[i] > peak * 0.5f)
				{
					const double since = static_cast<double>(i) - 10.0 * g_rate - latency;
					const double tick = std::round(since / framesPerTick - 0.5);	// the tick before it
					const double ms = (since - tick * framesPerTick) * 1000.0 / g_rate;
					sum += ms; lo = std::min(lo, ms); hi = std::max(hi, ms); ++n;
				}
			std::printf("issue #20: pulses at 120 BPM are %.2f ms after their tick on average (%.2f to %.2f), beyond the %d frames of latency\n",
				n ? sum / n : 0.0, n ? lo : 0.0, n ? hi : 0.0, p.getLatencySamples());
		};
		lag();
		std::printf("issue #20: %zu of %zu packets sent, %d ACKs; output 1: %.2f Hz at 120 BPM, %.2f Hz at 90 BPM, %.2f Hz stopped\n",
			sent, upload.size(), sender.acks(), r120, r90, stopped);
		if(!open || sent != upload.size() || std::abs(r120 - 48.0) > 0.5 || std::abs(r90 - 36.0) > 0.5 || stopped != 0.0)
		{
			std::printf("FAIL: the host's transport does not clock the G1\n");
			return 1;
		}
		return 0;
	}
}

namespace
{
	// JUCE's VST3 host wraps the plugin's own state, in base64, in an XML of its own: the plugin's
	// XML out of it, and back in.
	std::unique_ptr<juce::XmlElement> pluginXml(const juce::MemoryBlock& _state)
	{
		if(const auto xml = juce::AudioProcessor::getXmlFromBinary(_state.getData(), static_cast<int>(_state.getSize())))
			if(auto* component = xml->getChildByName("IComponent"))
			{
				juce::MemoryBlock inner;
				inner.fromBase64Encoding(component->getAllSubText());
				return juce::AudioProcessor::getXmlFromBinary(inner.getData(), static_cast<int>(inner.getSize()));
			}
		return {};
	}

	juce::MemoryBlock withPluginXml(const juce::MemoryBlock& _state, const juce::XmlElement& _plugin)
	{
		auto xml = juce::AudioProcessor::getXmlFromBinary(_state.getData(), static_cast<int>(_state.getSize()));
		auto* component = xml ? xml->getChildByName("IComponent") : nullptr;
		if(!component)
			return {};
		juce::MemoryBlock inner, out;
		juce::AudioProcessor::copyXmlToBinary(_plugin, inner);
		component->deleteAllTextElements();
		component->addTextElement(inner.toBase64Encoding());
		juce::AudioProcessor::copyXmlToBinary(*xml, out);
		return out;
	}

	// The panel's preferences in a plugin state, as they read.
	juce::String preferencesText(const juce::XmlElement& _xml)
	{
		juce::StringArray parts;
		for(const auto* key : {"panelScale", "extrasOpen", "knobDisplays", "knobFollowsPatch"})
			parts.add(juce::String(key) + " " + _xml.getStringAttribute(key, "(none)"));
		return parts.joinIntoString(", ");
	}

	// The synth settings section in a plugin state (<SynthSettings>, gzip + base64), and back.
	std::vector<uint8_t> settingsSection(const juce::XmlElement& _plugin)
	{
		const auto* x = _plugin.getChildByName("SynthSettings");
		juce::MemoryBlock packed;
		if(!x || !packed.fromBase64Encoding(x->getAllSubText()))
			return {};
		juce::MemoryInputStream in(packed, false);
		juce::GZIPDecompressorInputStream zip(in);
		juce::MemoryBlock plain;
		juce::MemoryOutputStream out(plain, false);
		out.writeFromInputStream(zip, -1);
		out.flush();
		const auto* d = static_cast<const uint8_t*>(plain.getData());
		return {d, d + plain.getSize()};
	}

	void setSettingsSection(juce::XmlElement& _plugin, const std::vector<uint8_t>& _section)
	{
		juce::MemoryOutputStream packed;
		{
			juce::GZIPCompressorOutputStream zip(packed, 9);
			zip.write(_section.data(), _section.size());
		}
		auto* x = _plugin.getChildByName("SynthSettings");
		if(!x)
			x = _plugin.createNewChildElement("SynthSettings");
		x->deleteAllTextElements();
		x->addTextElement(packed.getMemoryBlock().toBase64Encoding());
	}

	// Where the four slots' MIDI channels are in a section: after 7 bytes of globals and the name
	// (ended by a zero when shorter than 16), each channel followed by a marker. Empty if none.
	std::vector<size_t> channelOffsets(const std::vector<uint8_t>& _s)
	{
		if(_s.size() < 8 || _s[0] != 3)
			return {};
		size_t p = 7, n = 0;
		while(p < _s.size() && n < 16 && _s[p] != 0)
			++p, ++n;
		if(n < 16)
			++p;
		std::vector<size_t> at;
		for(int slot = 0; slot < 4 && p < _s.size(); ++slot, p += 2)
			at.push_back(p);
		return at.size() == 4 ? at : std::vector<size_t>{};
	}

	juce::String channelsText(const std::vector<uint8_t>& _s)
	{
		juce::StringArray parts;
		for(const auto p : channelOffsets(_s))
			parts.add(juce::String(_s[p] + 1));
		return parts.isEmpty() ? juce::String("(none)") : parts.joinIntoString(" ");
	}

	int parameterIndex(juce::AudioPluginInstance& _p, const juce::String& _name)
	{
		const auto params = _p.getParameters();
		for(int i = 0; i < params.size(); ++i)
			if(params[i]->getName(64) == _name)
				return i;
		return -1;
	}

	// The host sets parameter _index to _value and plays; then the project, saved, goes into a new
	// instance. What the parameter is after playing, and in the new instance.
	struct RoundTrip { float kept = -1, restored = -1; };
	RoundTrip roundTrip(juce::AudioPluginFormatManager& _formats, const juce::String& _path, Voice& _v, const int _index, const float _value)
	{
		auto* p = _v.plugin->getParameters()[_index];
		p->setValueNotifyingHost(_value);
		play({&_v}, 1.0);
		juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
		RoundTrip r;
		r.kept = p->getValue();
		juce::MemoryBlock state;
		_v.plugin->getStateInformation(state);
		Voice d;
		d.plugin = load(_formats, _path);
		d.plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
		play({&d}, 1.0);
		juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
		r.restored = d.plugin->getParameters()[_index]->getValue();
		return r;
	}

	// The plugin's preferences (what a new instance starts with) are the user's: kept as they are
	// while a check changes them, and put back however it ends.
	class KeepPreferences
	{
	public:
		KeepPreferences()
		// The per-user G1-Emu directory, as g1app::defaultSettingsPath has it.
	#if defined(_WIN32)
			: m_file(juce::File(juce::SystemStats::getEnvironmentVariable("APPDATA", {})).getChildFile("Animatek/G1-Emu/plugin.conf"))
	#else
			: m_file(juce::File("~/.local/share/Animatek/G1-Emu/plugin.conf"))
	#endif
			, m_had(m_file.existsAsFile()), m_text(m_had ? m_file.loadFileAsString() : juce::String())
		{
		}
		~KeepPreferences()
		{
			if(m_had)
				m_file.replaceWithText(m_text);
			else
				m_file.deleteFile();
		}
	private:
		const juce::File m_file;
		const bool m_had;
		const juce::String m_text;
	};

	std::unique_ptr<juce::AudioProcessorEditor> openEditor(juce::AudioPluginInstance& _p)
	{
		std::unique_ptr<juce::AudioProcessorEditor> editor(_p.createEditorIfNeeded());
		if(editor)
		{
			editor->addToDesktop(juce::ComponentPeer::windowHasTitleBar);
			editor->setVisible(true);
			juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);
		}
		return editor;
	}

	void closeEditor(juce::AudioPluginInstance& _p, std::unique_ptr<juce::AudioProcessorEditor>& _editor)
	{
		_p.editorBeingDeleted(_editor.get());
		_editor.reset();
		juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
	}

	// --window: the editor's size and the extras' settings live in the project. A window resized
	// by the host is saved at that size; a project with the extras open, the displays on and the
	// knobs following the patch opens like that, at the size it was saved at.
	// Issue #46 (--settings): the synth settings in use go into the project and come back from it,
	// stored with Shift+Store or not and under either OS. A project's slot A put on channel 5 by
	// hand must be on channel 5 in a new instance opened from it, as its own state then says
	// once the instance has read them back from the OS (so the OS took them). Issue #75: the same
	// with the slots that are on and the active one, only B in the project.
	juce::String slotsText(const std::vector<uint8_t>& _s, const size_t _at)
	{
		if(_at + 4 > _s.size() || _s[_at] != 0x07 || _s[_at + 2] != 0x09)
			return "none";
		juce::String t;
		for(int i = 0; i < 4; ++i)
			if(_s[_at + 1] & (1 << (3 - i)))
				t << juce::String::charToString(static_cast<juce::juce_wchar>('A' + i));
		return t + ", active " + juce::String::charToString(static_cast<juce::juce_wchar>('A' + (_s[_at + 3] & 3)));
	}

	int checkSettings(juce::AudioPluginFormatManager& _formats, const juce::String& _path)
	{
		Voice f;
		f.plugin = load(_formats, _path);
		drone(*f.plugin, 4.0);
		juce::MemoryBlock project;
		f.plugin->getStateInformation(project);
		f.plugin.reset();
		auto xml = pluginXml(project);
		const auto saved = xml ? settingsSection(*xml) : std::vector<uint8_t>{};
		std::printf("issue #46: a new instance saves the channels %s\n", channelsText(saved).toRawUTF8());
		const auto at = channelOffsets(saved);
		if(at.empty())
		{
			std::printf("FAIL: no synth settings in the project\n");
			return 1;
		}
		const size_t slotsAt = at[3] + 2;	// after slot D's channel and its marker
		std::printf("issue #75: and the slots on %s\n", slotsText(saved, slotsAt).toRawUTF8());
		if(slotsText(saved, slotsAt) == "none")
		{
			std::printf("FAIL: no slots in the project's synth settings\n");
			return 1;
		}

		auto changed = saved;
		changed[at[0]] = 4;		// slot A on channel 5
		changed[slotsAt + 1] = 0x04;	// only B on
		changed[slotsAt + 3] = 1;		// and active
		setSettingsSection(*xml, changed);
		Voice g;
		g.plugin = load(_formats, _path);
		const auto edited = withPluginXml(project, *xml);
		g.plugin->setStateInformation(edited.getData(), static_cast<int>(edited.getSize()));
		drone(*g.plugin, 8.0);	// the settings go in once the G1 is up (2.5 s), and are read back
		juce::MemoryBlock again;
		g.plugin->getStateInformation(again);
		const auto reopened = pluginXml(again);
		const auto back = reopened ? settingsSection(*reopened) : std::vector<uint8_t>{};
		std::printf("issue #46: the project with slot A on channel 5, reopened, saves the channels %s\n", channelsText(back).toRawUTF8());
		std::printf("issue #75: the project with only B on, reopened, saves the slots on %s\n", slotsText(back, slotsAt).toRawUTF8());
		if(back != changed)
		{
			std::printf("FAIL: the project's synth settings do not come back\n");
			return 1;
		}
		return 0;
	}

	// Issue #66 (--buffer-change), in a process of its own, as it hangs when it fails: Cubase and
	// Nuendo answer a latency change at once, in the same call stack, by deactivating and
	// reactivating the plugin. A new buffer size changes the latency inside prepareToPlay, so
	// setActive comes back into the plugin while it is still in there. Called on the plugin's
	// IComponent itself, as those hosts do: JUCE's host holds a lock of its own around its
	// prepareToPlay, and going through it would hang the host, not the plugin.
	struct ReactivateOnLatency final : juce::AudioProcessorListener
	{
		explicit ReactivateOnLatency(juce::AudioPluginInstance& _p) : p(_p) {}
		void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& _details) override
		{
			auto* component = static_cast<Steinberg::Vst::IComponent*>(p.getPlatformSpecificData());
			if(!_details.latencyChanged || inside || !component)
				return;
			inside = true;
			++count;
			component->setActive(false);
			component->setActive(true);
			inside = false;
		}
		void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
		juce::AudioPluginInstance& p;
		int count = 0;
		bool inside = false;
	};

	int checkBufferChange(juce::AudioPluginFormatManager& _formats, const juce::String& _path)
	{
		Voice f;
		f.plugin = load(_formats, _path);
		if(!f.plugin)
			return 1;
		auto& p = *f.plugin;
		p.setNonRealtime(true);
		ReactivateOnLatency host(p);
		p.addListener(&host);

		// Before the fix it never came back: a watchdog says so instead of hanging.
		std::atomic<bool> done{false};
		std::thread watchdog([&done]
		{
			for(int i = 0; i < 600 && !done; ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if(!done)
			{
				std::printf("FAIL: the plugin did not come back from a reactivation inside prepareToPlay (a deadlock)\n");
				std::fflush(stdout);
				std::_Exit(1);
			}
		});

		int failed = 0;
		for(const int block : {256, 1024, 128})
		{
			const int before = host.count;
			p.prepareToPlay(g_rate, block);
			const int latency = p.getLatencySamples();
			float peak = 0;
			for(int i = 0; i < 50; ++i)
			{
				juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), block);
				buffer.clear();
				juce::MidiBuffer midi;
				p.processBlock(buffer, midi);
				peak = std::max(peak, buffer.getMagnitude(0, 0, block));
			}
			std::printf("issue #66: buffer %d: reactivated %d time(s) inside prepareToPlay, latency %d frames, %.1f dBFS\n",
				block, host.count - before, latency, db(peak));
			if(latency < block)
			{
				std::printf("FAIL: the latency reported (%d) is less than the block\n", latency);
				failed = 1;
			}
		}
		if(host.count == 0)
		{
			std::printf("FAIL: no latency change was reported, so nothing was checked\n");
			failed = 1;
		}
		p.releaseResources();
		done = true;
		watchdog.join();
		p.removeListener(&host);
		return failed;
	}

	int checkWindow(juce::AudioPluginFormatManager& _formats, const juce::String& _path)
	{
		const KeepPreferences keep;
		int failed = 0;
		Voice a;
		a.plugin = load(_formats, _path);
		if(!a.plugin)
			return 1;
		play({&a}, 1.0);
		auto editor = openEditor(*a.plugin);
		if(!editor)
		{
			std::printf("FAIL: no editor\n");
			return 1;
		}
		std::printf("A's editor opens at %d x %d\n", editor->getWidth(), editor->getHeight());
		{
			juce::MemoryBlock opened;
			a.plugin->getStateInformation(opened);
			const auto x = pluginXml(opened);
			std::printf("  the plugin keeps panelScale %s after opening (a new instance: 1.25, 1500 wide)\n", x ? x->getStringAttribute("panelScale").toRawUTF8() : "?");
		}
		editor->setSize(1800, editor->getHeight() * 1800 / std::max(1, editor->getWidth()));	// as a host dragging its corner
		juce::MessageManager::getInstance()->runDispatchLoopUntil(800);
		const int resizedW = editor->getWidth(), resizedH = editor->getHeight();
		std::printf("resized by the host (asked for 1800 wide) to %d x %d\n", resizedW, resizedH);
		closeEditor(*a.plugin, editor);

		juce::MemoryBlock state;
		a.plugin->getStateInformation(state);
		auto xml = pluginXml(state);
		if(!xml)
		{
			std::printf("FAIL: the project's state is not the plugin's XML\n");
			return 1;
		}
		std::printf("saved: %s\n", preferencesText(*xml).toRawUTF8());
		// The size kept is the one the editor was given. (JUCE's own host window, this one, gives
		// a little less than it is asked for on Windows, about 3%: that is its business.)
		const double kept = xml->getDoubleAttribute("panelScale");
		if(std::abs(kept * 1200.0 - resizedW) > 0.01 * resizedW)
		{
			std::printf("FAIL: the size the host gave the editor (%d wide) is not the one in the project (%.0f)\n", resizedW, kept * 1200.0);
			failed = 1;
		}

		// The same project with the three extras on, into a new instance.
		xml->setAttribute("extrasOpen", true);
		xml->setAttribute("knobDisplays", true);
		xml->setAttribute("knobFollowsPatch", true);
		const auto project = withPluginXml(state, *xml);
		Voice b;
		b.plugin = load(_formats, _path);
		b.plugin->setStateInformation(project.getData(), static_cast<int>(project.getSize()));
		play({&b}, 1.0);
		editor = openEditor(*b.plugin);
		const int w = editor ? editor->getWidth() : 0, h = editor ? editor->getHeight() : 0;
		// With its drawer open the panel is 1200 x 591: those proportions, whatever this host's size.
		std::printf("reopened: the editor opens at %d x %d (the project kept %.0f wide; the drawer open is %.3f wide for 1 high)\n", w, h, kept * 1200.0, 1200.0 / 591.0);
		if(h <= 0 || std::abs(static_cast<double>(w) / h - 1200.0 / 591.0) > 0.01)
		{
			std::printf("FAIL: the reopened project's editor does not have its drawer open\n");
			failed = 1;
		}
		if(editor)
			closeEditor(*b.plugin, editor);
		juce::MemoryBlock again;
		b.plugin->getStateInformation(again);
		if(const auto x = pluginXml(again))
		{
			std::printf("saved again: %s\n", preferencesText(*x).toRawUTF8());
			if(!x->getBoolAttribute("extrasOpen") || !x->getBoolAttribute("knobDisplays") || !x->getBoolAttribute("knobFollowsPatch"))
			{
				std::printf("FAIL: the extras' settings do not survive a reopened project\n");
				failed = 1;
			}
		}

		// A new instance, with no project: it starts as the last editor was left (B's).
		Voice c;
		c.plugin = load(_formats, _path);
		play({&c}, 1.0);
		editor = openEditor(*c.plugin);
		const int cw = editor ? editor->getWidth() : 0, ch = editor ? editor->getHeight() : 0;
		std::printf("a new instance opens at %d x %d (the last editor was left at %d x %d)\n", cw, ch, w, h);
		if(editor)
			closeEditor(*c.plugin, editor);
		juce::MemoryBlock fresh;
		c.plugin->getStateInformation(fresh);
		const auto cx = pluginXml(fresh);
		if(ch <= 0 || std::abs(static_cast<double>(cw) / ch - 1200.0 / 591.0) > 0.01 || std::abs(cw - w) > 0.06 * w
			|| !cx || !cx->getBoolAttribute("knobDisplays") || !cx->getBoolAttribute("knobFollowsPatch"))
		{
			std::printf("FAIL: a new instance does not start as the last editor was left\n");
			failed = 1;
		}
		return failed;
	}
}

namespace
{
	// --micro-knobs: G1-Emu Micro's knobs (volume, edit knobs 1-3) go with the project. Its OS reads
	// them through the QSPI, which the project once did not keep: they came back at their defaults
	// each time (2026-11-03). A state with them set is opened in a new instance, which plays a while
	// and is saved again: the same values must be in it.
	int checkMicroKnobs(juce::AudioPluginFormatManager& _formats, const juce::String& _path)
	{
		Voice a;
		a.plugin = load(_formats, _path);
		if(!a.plugin)
			return 1;
		drone(*a.plugin, 3.0);
		juce::MemoryBlock state;
		a.plugin->getStateInformation(state);
		a.plugin.reset();
		auto xml = pluginXml(state);
		if(!xml)
		{
			std::printf("micro knobs: no plugin state\n");
			return 1;
		}
		const juce::String before = xml->getStringAttribute("qspi", "(none)");
		const juce::String wanted = "17 33 51 99 0 0 0 0 0 0 0 0 0 0 0 0";
		xml->setAttribute("qspi", wanted);
		const auto changed = withPluginXml(state, *xml);
		Voice b;
		b.plugin = load(_formats, _path);
		b.plugin->setStateInformation(changed.getData(), static_cast<int>(changed.getSize()));
		drone(*b.plugin, 4.0);
		juce::MemoryBlock again;
		b.plugin->getStateInformation(again);
		const auto back = pluginXml(again);
		const juce::String got = back ? back->getStringAttribute("qspi", "(none)") : "(no state)";
		std::printf("micro knobs: saved by a new instance \"%s\"; set to \"%s\", saved again \"%s\"\n",
			before.toRawUTF8(), wanted.toRawUTF8(), got.toRawUTF8());
		if(got != wanted)
		{
			std::printf("FAIL: the Micro's knobs do not come back with the project\n");
			return 1;
		}
		return 0;
	}

}

namespace
{
	// Issue #81 (--split-upload dir), in a process of its own: a patch (packet-NN.syx in dir) goes to
	// slot A over the direct link twelve times, 1.5 s apart, with its first packet in two halves
	// 200 ms apart, as a MIDI driver hands a long SysEx over in pieces. Meanwhile the plugin's own
	// links want the PC Port (the slot keeper reads slot A a second after each upload, the settings
	// link every 2 s): none may talk between the halves. A request there broke the editor's message
	// and the OS rejected the upload (sc $7E, "Unfinished bubble" or "Error in synth").
	int checkSplitUpload(juce::AudioPluginFormatManager& _formats, const juce::String& _path, const juce::File& _packets)
	{
		std::vector<std::vector<uint8_t>> upload;
		for(const auto& f : _packets.findChildFiles(juce::File::findFiles, false, "packet-*.syx"))
		{
			juce::MemoryBlock data;
			f.loadFileAsData(data);
			upload.emplace_back(static_cast<const uint8_t*>(data.getData()), static_cast<const uint8_t*>(data.getData()) + data.getSize());
		}
		if(upload.empty())
		{
			std::printf("issue #81: no packet-NN.syx in %s\n", _packets.getFullPathName().toRawUTF8());
			return 1;
		}
		Voice v;
		const auto taken = linkPortsTaken();
		v.plugin = load(_formats, _path);
		if(!v.plugin)
			return 1;
		auto& p = *v.plugin;
		juce::StreamingSocket link;
		const bool open = connectToNewLink(link, taken);
		std::vector<uint8_t> rx;
		bool greeted = false;
		const auto receive = [&]
		{
			uint8_t buf[512];
			while(open && link.waitUntilReady(true, 0) == 1)
			{
				const int n = link.read(buf, sizeof(buf), false);
				if(n <= 0)
					break;
				rx.insert(rx.end(), buf, buf + n);
			}
			if(!greeted)
				if(const auto nl = std::find(rx.begin(), rx.end(), '\n'); nl != rx.end())
				{
					rx.erase(rx.begin(), nl + 1);
					greeted = true;
				}
		};
		const auto count = [&](const bool _errors)
		{
			int n = 0;
			for(size_t i = 0; i + 6 < rx.size(); ++i)
				if(rx[i] == 0xf0 && rx[i + 1] == 0x33)
				{
					if(_errors && rx[i + 5] == 0x7e)
						++n;
					else if(!_errors && (rx[i + 2] >> 2) == 0x16 && (rx[i + 5] == 0x36 || rx[i + 5] == 0x7f))
						++n;
				}
			return n;
		};
		// In real time, block by block on the wall clock as a sound card calls, with the message loop
		// running in between: the plugin asks for the synth settings every 2 s from a timer.
		p.setNonRealtime(false);
		p.prepareToPlay(g_rate, g_block);
		auto next = std::chrono::steady_clock::now();
		const auto render = [&](const double _seconds, const std::function<bool()>& _until = {})
		{
			for(int b = 0; b < static_cast<int>(_seconds * g_rate / g_block); ++b)
			{
				receive();
				if(_until && _until())
					return;
				// Meanwhile the message loop runs: the plugin's timers ask its links for things, as in a DAW.
				next += std::chrono::microseconds(static_cast<int64_t>(1e6 * g_block / g_rate));
				const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(next - std::chrono::steady_clock::now()).count();
				if(left > 0)
					juce::MessageManager::getInstance()->runDispatchLoopUntil(static_cast<int>(left));
				juce::AudioBuffer<float> buffer(std::max(p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()), g_block);
				buffer.clear();
				juce::MidiBuffer midi;
				p.processBlock(buffer, midi);
			}
		};
		render(6.0);	// the G1 boots
		int rejected = 0, accepted = 0;
		std::string results;
		for(int u = 0; u < 12 && open; ++u)
		{
			rx.clear();
			bool failed = false;
			for(size_t k = 0; k < upload.size() && !failed; ++k)
			{
				const auto& packet = upload[k];
				if(k == 0)
				{
					const int half = static_cast<int>(packet.size() / 2);
					link.write(packet.data(), half);
					juce::Thread::sleep(5);
					render(0.2);
					link.write(packet.data() + half, static_cast<int>(packet.size()) - half);
				}
				else
					link.write(packet.data(), static_cast<int>(packet.size()));
				juce::Thread::sleep(5);
				render(3.0, [&] { return count(false) > static_cast<int>(k) || count(true) > 0; });
				failed = count(true) > 0;
			}
			render(0.3);
			failed |= count(true) > 0;
			results += failed ? " R" : " ok";
			(failed ? rejected : accepted)++;
			render(1.5);
		}
		p.releaseResources();
		std::printf("issue #81: direct link %s; 12 uploads with the first packet split:%s\n", open ? "open" : "NOT THERE", results.c_str());
		std::printf("issue #81: %d accepted, %d rejected by the synth\n", accepted, rejected);
		return !open || rejected ? 1 : 0;
	}
}

int main(int _argc, char** _argv)
{
	if(_argc < 2)
	{
		std::printf("usage: g1vst3check path/to/G1-Emu.vst3\n");
		return 2;
	}
	juce::ScopedJuceInitialiser_GUI juce;
	juce::AudioPluginFormatManager formats;
	formats.addFormat(new juce::VST3PluginFormat());
	const juce::String path(_argv[1]);
	int failed = 0;
	// No PC Port is needed for it, so none is made, and another G1-Emu open is no matter.
	if(_argc > 2 && juce::String(_argv[2]) == "--buffer-change")
	{
#if defined(_WIN32)
		_putenv_s("G1_PLUGIN_PC_PORT", "0");
#else
		setenv("G1_PLUGIN_PC_PORT", "0", 1);
#endif
		failed = checkBufferChange(formats, path);
		std::printf("%s\n", failed ? "FAILED" : "all good");
		return failed;
	}
	// The PC Ports are found by name, and another G1-Emu (a DAW with the plugin open) has the same
	// names: the check would greet that one and upload patches to its slot A.
	for(const auto& d : juce::MidiOutput::getAvailableDevices())
		if(d.name.startsWith("G1-Emu"))
		{
			std::printf("another G1-Emu is running (\"%s\"): close it first, or this check would talk to it\n", d.name.toRawUTF8());
			return 2;
		}
	if(_argc > 2 && juce::String(_argv[2]) == "--slots")
	{
		failed = checkSlots(formats, path);
		std::printf("%s\n", failed ? "FAILED" : "all good");
		return failed;
	}
	if(_argc > 3 && juce::String(_argv[2]) == "--clock")
	{
		failed = checkClock(formats, path, juce::File(juce::String(_argv[3])));
		std::printf("%s\n", failed ? "FAILED" : "all good");
		return failed;
	}
	if(_argc > 3 && juce::String(_argv[2]) == "--split-upload")
	{
		failed = checkSplitUpload(formats, path, juce::File(juce::String(_argv[3])));
		std::printf("%s\n", failed ? "FAILED" : "all good");
		return failed;
	}
	if(_argc > 3 && juce::String(_argv[2]) == "--fx")
	{
		failed = checkFx(formats, path, juce::File(juce::String(_argv[3])));
		std::printf("%s\n", failed ? "FAILED" : "all good");
		return failed;
	}
	if(_argc > 2 && juce::String(_argv[2]) == "--micro-knobs")
	{
		failed = checkMicroKnobs(formats, path);
		std::printf("%s\n", failed ? "FAILED" : "all good");
		return failed;
	}
	if(_argc > 2 && juce::String(_argv[2]) == "--settings")
	{
		failed = checkSettings(formats, path);
		std::printf("%s\n", failed ? "FAILED" : "all good");
		return failed;
	}
	if(_argc > 2 && juce::String(_argv[2]) == "--window")
	{
		failed = checkWindow(formats, path);
		std::printf("%s\n", failed ? "FAILED" : "all good");
		return failed;
	}

	const KeepPreferences keep;	// the editor opened at the end saves its size as a new instance's
	Voice a, b;
	a.plugin = load(formats, path);
	b.plugin = load(formats, path);
	if(!a.plugin || !b.plugin)
		return 1;
	auto& p = *a.plugin;
	std::printf("%s by %s: %d in, %d out (%d buses), accepts MIDI %d, synth %d\n", p.getName().toRawUTF8(),
		p.getPluginDescription().manufacturerName.toRawUTF8(), p.getTotalNumInputChannels(), p.getTotalNumOutputChannels(),
		p.getBusCount(false), p.acceptsMidi(), p.getPluginDescription().isInstrument);

	// The editor on both PC Ports. The greeting goes once a second from 1 s on: the OS may still
	// be booting at the first one. Rendering is offline, so the replies get time to come back by
	// sleeping a little at each greeting.
	Editor editor;
#if defined(_WIN32)
	const bool havePorts = false;
#else
	const bool havePorts = editor.open("G1-Emu PC Port") && editor.open("G1-Emu 2 PC Port");
	if(!havePorts)
	{
		std::printf("FAIL: the instances' PC Ports (\"G1-Emu PC Port\", \"G1-Emu 2 PC Port\") are not there\n");
		failed = 1;
	}
#endif
	auto greet = [&](const int _pos)
	{
		if(!havePorts || _pos < static_cast<int>(g_rate) || _pos % static_cast<int>(g_rate) >= g_block)
			return;
		editor.greet("G1-Emu PC Port");
		juce::Thread::sleep(100);
	};

	a.program = 0;
	b.program = 1;
	play({&a, &b}, 5.5, greet);
	if(havePorts)
	{
		juce::Thread::sleep(200);
		const int ra = editor.replies("G1-Emu PC Port"), rb = editor.replies("G1-Emu 2 PC Port");
		std::printf("PC Port: A answered IAm %d times, B %d (only A was greeted)\n", ra, rb);
		if(ra == 0 || rb != 0)
		{
			std::printf("FAIL: the PC Port does not answer, or answers on the wrong instance\n");
			failed = 1;
		}
	}
	const auto latency = p.getLatencySamples();
	std::printf("two instances at once: A (program 1) %.1f dBFS before the note, %.1f after; B (program 2) %.1f / %.1f; latency %d frames\n",
		db(a.peakBefore), db(a.peakAfter), db(b.peakBefore), db(b.peakAfter), latency);
	if(a.peakAfter < 1e-3f || b.peakAfter < 1e-3f)
	{
		std::printf("FAIL: an instance is silent after the note (is there a ROM, and patches in the standalone's banks?)\n");
		failed = 1;
	}

	juce::MemoryBlock state;
	p.getStateInformation(state);
	std::printf("state of A: %zu bytes\n", state.getSize());

	// The state into a new instance, before it starts, as a DAW opening a project does. No
	// Program Change this time: the plugin must send the one it remembered from A by itself.
	Voice c;
	c.plugin = load(formats, path);
	c.plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
	// A reopened project holds its notes back while its slots and synth settings go back in (#46),
	// about 3.8 s of the G1's time: past this first play's note, so that one is let go, and with no
	// message loop running, as some hosts render, the plugin has to finish it by itself.
	play({&c}, 5.5);
	play({&c}, 5.5);
	std::printf("restored into C: latency %d frames, %.1f dBFS after the note (no Program Change sent)\n",
		c.plugin->getLatencySamples(), db(c.peakAfter));
	juce::MemoryBlock again;
	c.plugin->getStateInformation(again);
	std::printf("state of C: %zu bytes\n", again.getSize());
	if(c.plugin->getLatencySamples() != latency || again.isEmpty() || c.peakAfter < 1e-3f)
	{
		std::printf("FAIL: the restored instance does not match or does not play\n");
		failed = 1;
	}

	// The mono outputs (issue #27), turned on beside the stereo pairs as Cubase's Activate Outputs
	// does: Out 1..4 must carry exactly what Out 1/2 and Out 3/4 carry.
	{
		Voice m;
		m.plugin = load(formats, path);
		auto& mp = *m.plugin;
		const bool enabled = mp.enableAllBuses();
		mp.setNonRealtime(true);
		mp.prepareToPlay(g_rate, g_block);
		const int channels = mp.getTotalNumOutputChannels();
		float peak = 0, worst = 0;
		for(int pos = 0; pos < static_cast<int>(4.0 * g_rate); pos += g_block)
		{
			juce::AudioBuffer<float> buffer(std::max(mp.getTotalNumInputChannels(), channels), g_block);
			buffer.clear();
			juce::MidiBuffer midi;
			const int pcAt = static_cast<int>(2.0 * g_rate), onAt = static_cast<int>(3.0 * g_rate);
			if(pcAt >= pos && pcAt < pos + g_block)
				midi.addEvent(juce::MidiMessage::programChange(1, 0), pcAt - pos);
			if(onAt >= pos && onAt < pos + g_block)
				midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), onAt - pos);
			mp.processBlock(buffer, midi);
			if(channels < 8)
				continue;
			peak = std::max(peak, buffer.getMagnitude(0, 0, g_block));
			for(int c = 0; c < 4; ++c)
				for(int i = 0; i < g_block; ++i)
					worst = std::max(worst, std::abs(buffer.getSample(4 + c, i) - buffer.getSample(c, i)));
		}
		mp.releaseResources();
		std::printf("mono outputs: %d buses, %d channels with all on; out 1 %.1f dBFS, largest difference mono/stereo %g\n",
			mp.getBusCount(false), channels, db(peak), static_cast<double>(worst));
		if(!enabled || channels != 8 || peak < 1e-3f || worst != 0.0f)
		{
			std::printf("FAIL: the mono outputs\n");
			failed = 1;
		}
	}

	// The 18 knobs as parameters: the host turns knob 1, the G1 keeps it there (nothing turns it
	// back), and a project saved after that brings it back in a new instance.
	{
		const auto params = c.plugin->getParameters();
		int knobs = 0;
		for(auto* prm : params)
			if(prm->getName(64).startsWith("Knob "))
				++knobs;
		juce::MessageManager::getInstance()->runDispatchLoopUntil(300);	// the names follow the patch
		std::printf("knob parameters: %d; knob 1 is \"%s\"\n", knobs, params.isEmpty() ? "" : params[0]->getName(64).toRawUTF8());
		const auto r = roundTrip(formats, path, c, 0, 0.75f);
		std::printf("knob 1 set to 0.75 by the host: %.3f after playing (%s), %.3f in a project reopened\n",
			r.kept, params[0]->getCurrentValueAsText().toRawUTF8(), r.restored);
		if(knobs != 18 || std::abs(r.kept - 0.75f) > 0.005f || std::abs(r.restored - 0.75f) > 0.005f)
		{
			std::printf("FAIL: the knobs as parameters\n");
			failed = 1;
		}
	}

	// A slot's octave shift as a parameter: the host sets slot A's to +1, the G1 takes it (its
	// patch carries it, so the keeper reads slot A again), and a project saved after that brings
	// it back in a new instance once its slots are restored. Slower than a knob's: the keeper
	// reads the slot after a quiet second, and a reopened project restores its slots first.
	{
		const int oct = parameterIndex(*c.plugin, "Slot A Octave");
		auto settle = [](Voice& _v, const int _steps)
		{
			for(int i = 0; i < _steps; ++i)
			{
				play({&_v}, 0.5);
				juce::MessageManager::getInstance()->runDispatchLoopUntil(300);	// the plugin's timers, as in a DAW
			}
		};
		auto shift = [&](Voice& _v) { return juce::roundToInt(_v.plugin->getParameters()[oct]->getValue() * 4.0f) - 2; };
		int kept = -9, restored = -9;
		if(oct >= 0)
		{
			c.plugin->getParameters()[oct]->setValueNotifyingHost(0.75f);	// +1
			settle(c, 12);
			kept = shift(c);
			juce::MemoryBlock state;
			c.plugin->getStateInformation(state);
			Voice d;
			d.plugin = load(formats, path);
			d.plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
			settle(d, 20);
			restored = shift(d);
			c.plugin->getParameters()[oct]->setValueNotifyingHost(0.5f);	// back to 0 for what follows
			settle(c, 2);
		}
		std::printf("slot A's octave set to +1 by the host: %+d after playing, %+d in a project reopened\n", kept, restored);
		if(oct < 0 || kept != 1 || restored != 1)
		{
			std::printf("FAIL: the octave shift as a parameter\n");
			failed = 1;
		}
	}

	// The master volume as a parameter, the same way: 0-127, set to 64 by the host.
	{
		const int vol = parameterIndex(*c.plugin, "Master Volume");
		const auto r = vol < 0 ? RoundTrip{} : roundTrip(formats, path, c, vol, 64.0f / 127.0f);
		std::printf("master volume set to 64 by the host: %.0f after playing, %.0f in a project reopened\n", r.kept * 127.0f, r.restored * 127.0f);
		if(vol < 0 || std::abs(r.kept * 127.0f - 64.0f) > 0.01f || std::abs(r.restored * 127.0f - 64.0f) > 0.01f)
		{
			std::printf("FAIL: the master volume as a parameter\n");
			failed = 1;
		}
	}

	// A Program Change through the host's program parameter, which is how a VST3 host sends one
	// (VST3 has no Program Change as MIDI): it must reach the G1 on channel 1, and the project
	// remembers it like one from the track.
	{
		Voice e;
		e.plugin = load(formats, path);
		e.plugin->setCurrentProgram(4);
		play({&e}, 1.0);
		juce::MemoryBlock st;
		e.plugin->getStateInformation(st);
		// JUCE's VST3 host wraps the plugin's own state, in base64, in an XML of its own.
		bool remembered = false;
		if(const auto xml = juce::AudioProcessor::getXmlFromBinary(st.getData(), static_cast<int>(st.getSize())))
			if(auto* component = xml->getChildByName("IComponent"))
			{
				juce::MemoryBlock inner;
				inner.fromBase64Encoding(component->getAllSubText());
				const std::string bytes(static_cast<const char*>(inner.getData()), inner.getSize());
				remembered = bytes.find("programs=\"0:-1:-1:4") != std::string::npos;
			}
		std::printf("host program 5: current %d, %s in the project\n", e.plugin->getCurrentProgram(), remembered ? "remembered" : "NOT remembered");
		if(!remembered || e.plugin->getCurrentProgram() != 4)
		{
			std::printf("FAIL: the host's program does not reach the G1\n");
			failed = 1;
		}
	}

	if(juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
	{
		std::unique_ptr<juce::AudioProcessorEditor> editor(c.plugin->createEditorIfNeeded());
		if(editor)
		{
			editor->addToDesktop(juce::ComponentPeer::windowHasTitleBar);
			editor->setVisible(true);
			juce::MessageManager::getInstance()->runDispatchLoopUntil(1500);
			std::printf("editor: %d x %d\n", editor->getWidth(), editor->getHeight());
			c.plugin->editorBeingDeleted(editor.get());
			editor.reset();
		}
		else
		{
			std::printf("FAIL: no editor\n");
			failed = 1;
		}
	}

	a.plugin.reset();
	b.plugin.reset();
	c.plugin.reset();
	std::printf("%s\n", failed ? "FAILED" : "all good");
	return failed;
}
