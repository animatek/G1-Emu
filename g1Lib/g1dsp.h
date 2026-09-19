#pragma once

// One of the G1's DSP56303s, emulated with dsp56kEmu, attached to the HI08 register the CPU sees.
//
// The G1 boots its DSPs through the host port (HI08): the DSP boot ROM receives length,
// address and words, and jumps to the program. DSP 3 first gets a short loader program
// (PLL, serial ports, codec) that ends with `jmp $FF0000`, i.e. it returns to its boot ROM
// to receive the sound program afterwards. That jump is detected here and the boot re-armed.
//
//
// The CPU drives: each DSP catches up (catchUp) to the CPU's time, on its own thread or on
// the CPU thread (see g1mc.h), and whenever the CPU reads or writes its port.

#include "dsp56kEmu/dsp.h"
#include "dsp56kEmu/dspBootCode.h"
#include "dsp56kEmu/memory.h"
#include "dsp56kEmu/peripherals.h"

#include <cstdint>
#include <array>
#include <functional>
#include <deque>
#include <map>
#include <memory>
#include <vector>

namespace mc68k { class Hdi08; }

namespace g1
{
	class Dsp
	{
	public:
		Dsp(mc68k::Hdi08& _hdiUc, uint32_t _index);

		dsp56k::DSP& dsp() { return m_dsp; }
		dsp56k::HDI08& hdi08() { return m_periph.getHI08(); }
		dsp56k::Peripherals56303& periph() { return m_periph; }

		bool booted() const { return m_booted; }
		uint32_t bootCount() const { return m_bootCount; }
		uint32_t index() const { return m_index; }
		uint64_t stalls() const { return m_stalls; }
		uint64_t audioFrames() const { return m_audioFrames; }
		uint64_t hostWords() const { return m_hostWords; }
		uint64_t irqdCount() const { return m_irqdCount; }
		uint64_t laChanges() const { return m_laChanges; }	// times a loop end has been moved
		const std::map<uint32_t, uint64_t>& servicedVectors() const { return m_servicedVectors; }
		uint32_t lastVector() const { return m_lastVector; }
		std::map<uint32_t, uint64_t>& pcWatch() { return m_pcWatch; }
		uint64_t hostCommands() const { return m_hostCommands; }
		uint64_t wordsToHost() const { return m_wordsToHost; }

		// Meter: peak (absolute value, signed 24-bit) per ESSI, slot and TX line since the
		// last reset. Useful to find out where the audio comes out.
		static constexpr uint32_t MeterSlots = 4, MeterLines = 3;
		using Meter = std::array<std::array<std::array<uint32_t, MeterLines>, MeterSlots>, 2>;
		const Meter& meter() const { return m_meter; }
		void resetMeter() { m_meter = {}; }
		uint32_t lastSlotCount(uint32_t _essi) const { return m_slotCount[_essi]; }

		// Receives every frame leaving an ESSI: (essi, TX0 slot 0, TX0 slot 1), signed 24-bit.
		// Useful to record or replay the output.
		using AudioCallback = std::function<void(uint32_t, int32_t, int32_t)>;
		void setAudioCallback(AudioCallback _cb) { m_audioCallback = std::move(_cb); }

		// Audio chain: what leaves this DSP's ESSIs enters the next DSP's.
		void setNext(Dsp* _next) { m_next = _next; if(_next) _next->m_hasUpstream = true; }
		uint64_t chainedFrames() const { return m_chainedFrames; }

		// Peak of each channel this DSP sends to the next (9 on ESSI0 and 9 on ESSI1), 24-bit,
		// since the last reset. Tells which DSP carries the voice and on which channel.
		using LinkPeak = std::array<uint32_t, 18>;
		const LinkPeak& linkPeak() const { return m_linkPeak; }
		void resetLinkPeak() { m_linkPeak = {}; }

		// Receives each block's output (one sample at 96 kHz): outputs 1/2 (ESSI0) and 3/4
		// (ESSI1), signed 24-bit. With DSP 3's sound program, that is what goes to the codec.
		// Delivered in flushAudio.
		using BlockCallback = std::function<void(int32_t, int32_t, int32_t, int32_t)>;
		void setBlockCallback(BlockCallback _cb) { m_blockCallback = std::move(_cb); }

		// Audio inputs (L, R), signed 24-bit: requested once per block (96 kHz), on this DSP's
		// thread. Only the first DSP of the chain uses them: it is the one that receives the codec.
		using InputProvider = std::function<void(int32_t&, int32_t&)>;
		void setInputProvider(InputProvider _p) { m_inputProvider = std::move(_p); }

		// Runs the DSP up to _cycles cycles (or up to a limit if it is waiting).
		// It can run on its own thread: what leaves the ESSIs stays in its own queue.
		void catchUp(uint64_t _cycles);

		// Hands on what left the ESSIs since the last time: to the next DSP's input and to the
		// audio callback. Only from the CPU thread, with the DSPs stopped.
		void flushAudio();

	private:
		void armBoot();
		void onBootFinished();
		void hostWord(uint32_t _word);
		void hostCommand(uint8_t _vector);
		uint8_t readIsr(uint8_t _isr);
		bool transferToHost();
		void runUntil(uint64_t _cycles);
		void drainAudio();
		bool irqdEnabled();
		void tapBlock();
		void onLaChanged();
		void tapLink(uint64_t _block);
		void readLink(uint32_t _essi, dsp56k::Audio::RxFrame& _frame);

		mc68k::Hdi08& m_hdiUc;
		const uint32_t m_index;

		dsp56k::DefaultMemoryValidator m_validator;
		dsp56k::PeripheralsNop m_periphNop;
		dsp56k::Peripherals56303 m_periph;
		dsp56k::Memory m_memory;
		dsp56k::DSP m_dsp;
		std::unique_ptr<dsp56k::DspBoot> m_boot;

		bool m_booted = false;
		bool m_interpreter = false;	// G1_INTERP
		dsp56k::TWord m_lastLa = 0;
		bool m_noLaFix = false;
		uint64_t m_laChanges = 0;
		uint32_t m_bootCount = 0;
		uint64_t m_stalls = 0;
		uint64_t m_audioFrames = 0;
		uint64_t m_hostWords = 0, m_hostCommands = 0, m_wordsToHost = 0;
		uint64_t m_nextIrqd = 0, m_irqdCount = 0;
		std::map<uint32_t, uint64_t> m_servicedVectors;
		uint32_t m_lastVector = 0;
		std::map<uint32_t, uint64_t> m_pcWatch;	// PCs to watch (diagnostics only)
		Meter m_meter{};
		AudioCallback m_audioCallback;
		Dsp* m_next = nullptr;
		bool m_hasUpstream = false;
		struct LinkBlock { uint64_t index; std::array<dsp56k::TWord, 18> words; };
		std::vector<LinkBlock> m_linkOut;	// this DSP's blocks, pending flushAudio
		std::deque<LinkBlock> m_linkIn;		// blocks from the previous DSP
		LinkPeak m_linkPeak{};
		std::array<dsp56k::TWord, 2> m_craSeen{};
		BlockCallback m_blockCallback;
		InputProvider m_inputProvider;
		std::array<int32_t, 2> m_input{};
		using BlockFrame = std::array<dsp56k::TWord, 4>;
		std::vector<BlockFrame> m_blocks;	// pending flushAudio
		struct StagedFrame { uint32_t slots; std::array<dsp56k::TWord, 4> v; };
		std::array<std::vector<StagedFrame>, 2> m_staged;	// per ESSI, pending flushAudio
		uint64_t m_chainedFrames = 0;
		std::array<uint32_t, 2> m_slotCount{};
	};
}
