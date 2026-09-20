#pragma once

// The Nord Modular G1's CPU: a Motorola 68331 (CPU32) emulated with Gearmulator's Musashi,
// like the Nord Lead 2X's. It holds the memory map (ROM, RAM, flash), the four DSPs on their
// HI08 ports, the PC PORT DUART and the panel (display, LEDs, buttons, ADC). Accesses to
// anything else are logged as "unknown", which is how the memory map was found.

#include "mc68k/mc68k.h"

#include "g1dsp.h"
#include "g1flash.h"
#include "g1duart.h"
#include "g1lcd.h"

#include "mc68k/hdi08.h"
#include "hardwareLib/sciMidi.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace g1
{
	// The reset vector's stack points to $1FFF00: ROM at $000000 (512 KB) and RAM up to
	// $1FFFFF, as the chip-selects programmed at boot confirm.
	static constexpr uint32_t g_romSize = 0x80000;
	static constexpr uint32_t g_memSize = 0x200000;

	// What the loader programs into the chip-selects (see NOTES.md):
	static constexpr uint32_t g_ramAddress = 0x100000;		// CS8-10, 1 MB, 16 bits
	static constexpr uint32_t g_dspAddress = 0x200000;		// CS0, the DSPs' HI08 ports
	static constexpr uint32_t g_panelIn = 0x201800;			// CS4, button matrix input
	static constexpr uint32_t g_panelOut = 0x202000;		// CS2, ADC multiplexer
	static constexpr uint32_t g_panelAdc = 0x202800;		// CS3, knob ADC (channel selected in $202000)
	static constexpr uint32_t g_panelLeds = 0x202004;		// LEDs: the 8 of the selected row
	static constexpr uint32_t g_panelRows = 0x202005;		// rows: LEDs (bits 0-3) and buttons (4-6, active low)
	static constexpr uint32_t g_lcdData = 0x202006;		// HD44780 LCD: data bus
	static constexpr uint32_t g_lcdControl = 0x202007;		// LCD: bit 0 = RS, bit 1 = E
	static constexpr uint8_t g_adcVolume = 0x30;			// multiplexer code of the master volume
	static constexpr uint32_t g_flashAddress = 0x300000;	// CS7, 1 MB, 8 bits: the installed OS
	static constexpr uint32_t g_flashSize = Flash::Size;

	// The factory OS is inside the ROM, at $C800, built to run at $100000.
	// The loader copies it to RAM if a certain key combination is held at power-on; with no
	// keys, it copies the one in the flash at $300000 (length at +8, data from +$20).
	static constexpr uint32_t g_romOsOffset = 0xc800;
	static constexpr uint32_t g_romOsLongs = 0x1ce01;

	// 8 HI08 ports at $200000 + 8*n: DSPs 0-3 on the main board, 4-7 on the expansion.
	static constexpr uint32_t g_hostPorts = 8;
	static constexpr uint32_t g_dspCount = 4;			// G1 without the expansion board
	// The DSPs run at 12.288 MHz x 27/4 = 82.944 MHz (PCTL=$3C001A, the same on all four):
	// 864 cycles per sample at 96 kHz, which is exactly what the links between DSPs need
	// (9 words of 96 cycles per block). Against the CPU's 20.97 MHz: 2025/512.
	static constexpr uint32_t g_dspClock = 82944000;
	static constexpr uint64_t g_dspCyclesPerUcNum = 2025, g_dspCyclesPerUcDen = 512;
	static constexpr uint32_t g_ucClock = 20971520;			// SYNCR=$D300 with a 32768 Hz crystal
	static constexpr uint32_t g_sciRate = 44100;			// rate at which SciMidi moves bytes
	static constexpr uint32_t g_ucCyclesPerSciSample = g_ucClock / g_sciRate;
	static constexpr uint32_t g_ucCyclesPerSerialByte = g_ucClock / 3125;	// 10 bits at 31250 baud

	struct UnknownAccess
	{
		uint32_t reads = 0;
		uint32_t writes = 0;
		uint32_t firstPc = 0;
		uint32_t lastValue = 0;
	};

	class Microcontroller final : public mc68k::Mc68k
	{
	public:
		static constexpr uint32_t g_pcTrail = 256;
		explicit Microcontroller(const std::vector<uint8_t>& _rom);
		~Microcontroller() override;

		uint32_t exec() override;
		uint16_t readImm16(uint32_t _addr) override;
		uint16_t read16(uint32_t _addr) override;
		uint8_t read8(uint32_t _addr) override;
		void write16(uint32_t _addr, uint16_t _val) override;
		void write8(uint32_t _addr, uint8_t _val) override;

		// Accesses outside ROM/RAM and the internal peripherals, by address.
		const std::map<uint32_t, UnknownAccess>& unknownAccesses() const { return m_unknown; }
		uint32_t romWrites() const { return m_romWrites; }

		// Trace of the accesses to the HI08 ports ($200000-$20003F), in order.
		struct HostAccess { uint32_t addr; uint32_t value; uint32_t pc; bool write; uint8_t size; };
		std::vector<HostAccess>& hostTrace() { return m_hostTrace; }

		// Puts the ROM's factory OS in the flash, as an update would.
		void installRomOsInFlash();
		Flash& getFlash() { return m_flash; }
		Dsp& getDsp(uint32_t _i) { return *m_dsps[_i]; }
		mc68k::Hdi08& getHostPort(uint32_t _i) { return m_hostPorts[_i]; }

		// The CPU's UART (the QSM's SCI, 31250 baud): the regular MIDI IN/OUT.
		//
		hwLib::SciMidi& getSci() { return m_sci; }
		uint32_t sciDataReads() const { return m_sciDataReads; }

		// PC PORT (the editor's): DUART on a parallel bus, see g1duart.h.
		Duart& getPcPort() { return m_pcPort; }
		uint32_t pcPortIrqs() const { return m_pcPortIrqs; }
		uint32_t sciDataWrites() const { return m_sciDataWrites; }
		uint64_t ucCycles() const { return m_ucCycles; }

		// The panel: display, 32 LEDs (4 rows of 8), 18 buttons (3 rows of bits 2-7), the dial
		// (bits 0 and 1 of the same input) and the knobs, which are ADC channels (setAdc).
		// All of it can be read and written from another thread.
		const Lcd& getLcd() const { return m_lcd; }
		uint8_t ledRow(const uint32_t _row) const { return m_leds[_row & 3].load(std::memory_order_relaxed); }
		void setButton(const uint32_t _row, const uint32_t _bit, const bool _pressed)
		{
			auto& r = m_buttons[_row % 3];
			const auto mask = static_cast<uint8_t>(1u << (_bit & 7));
			_pressed ? r.fetch_or(mask) : r.fetch_and(static_cast<uint8_t>(~mask));
		}
		// The dial, in detents: positive clockwise and negative anticlockwise. Each detent is
		// four edges of the quadrature pair, handed out one by one as the
		// OS reads the panel.
		void turnDial(const int32_t _detents) { m_dialEdges.fetch_add(_detents * 4); }

	private:
		bool isInternalPeripheral(uint32_t _addr) const { return (_addr & 0xfff000) == 0xfff000; }
		static bool isHostPort(uint32_t _addr) { return _addr >= g_dspAddress && _addr < g_dspAddress + g_hostPorts * 8; }
		mc68k::Hdi08& hostPort(uint32_t _addr) { return m_hostPorts[(_addr - g_dspAddress) >> 3]; }
		static mc68k::PeriphAddress hostReg(uint32_t _addr) { return static_cast<mc68k::PeriphAddress>(_addr & 7); }
		void traceHost(uint32_t _addr, bool _write, uint32_t _value);
		void catchUpDsps();
		void onPortE(uint8_t _value);
		void execPcPort();
		void execPit(uint32_t _cycles);
		void logUnknown(uint32_t _addr, bool _write, uint32_t _value);

		std::vector<uint8_t> m_mem;		// ROM + RAM in one block
		Flash m_flash;					// $300000
		std::map<uint32_t, UnknownAccess> m_unknown;
		std::vector<HostAccess> m_hostTrace;
		std::array<mc68k::Hdi08, g_hostPorts> m_hostPorts;
		std::array<std::unique_ptr<Dsp>, g_dspCount> m_dsps;
		uint64_t m_ucCycles = 0;
		hwLib::SciMidi m_sci;
		uint64_t m_nextSciSample = 0;
		uint32_t m_sciDataReads = 0;
		Duart m_pcPort;
		uint8_t m_prevPortE = 0xff;
		uint64_t m_nextPcPortByte = 0;
		uint32_t m_pcPortIrqs = 0;
		// The SIM's periodic interrupt timer (PIT): the OS's system clock.
		uint16_t m_picr = 0, m_pitr = 0;
		std::array<uint8_t, 256> m_adc{};	// set in the constructor: volume at full, knobs at zero
		uint8_t m_adcSelect = 0, m_adcResult = 0;
		uint8_t m_ledLatch = 0, m_panelRows = 0xff;
		std::array<std::atomic<uint8_t>, 4> m_leds{};
		std::array<std::atomic<uint8_t>, 3> m_buttons{};	// 1 = pressed
		uint8_t buttonRow() const;
		// The dial: a quadrature encoder on bits 0 and 1, decoded by the OS at $104DC6 (four
		// edges per detent). One edge every g_dialEdgeCycles, well apart from each other: the
		// OS reads the panel some 3 000 times a second and ignores any step of two edges.
		static constexpr uint64_t g_dialEdgeCycles = 40000;	// ~2 ms at 20.97 MHz
		mutable std::atomic<int32_t> m_dialEdges{0};	// pending edges, signed
		mutable uint8_t m_dialPhase = 0;
		mutable uint64_t m_nextDialEdge = 0;
		uint8_t dialBits() const;
		Lcd m_lcd;
		uint64_t m_pitAccum = 0;

		// One thread per DSP (DSP 0 runs on the CPU thread). At each sync the CPU publishes the
		// target cycle and the DSPs run in parallel up to it; G1_THREADS=0 runs everything serially.
		void workerLoop(uint32_t _dsp);
		bool m_threaded = true;
		std::vector<std::thread> m_workers;
		std::atomic<uint64_t> m_generation{0};
		std::atomic<uint32_t> m_pending{0};
		std::atomic<uint32_t> m_sleepers{0};
		std::atomic<bool> m_quitWorkers{false};
		uint64_t m_dspTarget = 0;
		std::mutex m_wakeMutex;
		std::condition_variable m_wake;
		uint64_t m_pitIrqs = 0;
		std::array<std::atomic<uint32_t>, g_pcTrail> m_pcTrail{};
		std::atomic<uint32_t> m_pcTrailPos{0};
	public:
		uint64_t pitIrqs() const { return m_pitIrqs; }
		// A trail of the last PCs, sampled every ~1 000 cycles by the emulation thread. It is
		// what tells, when the G1 stops answering, which loop the OS is stuck in.
		std::array<uint32_t, g_pcTrail> pcTrail() const
		{
			std::array<uint32_t, g_pcTrail> out{};
			const auto pos = m_pcTrailPos.load(std::memory_order_relaxed);
			for(uint32_t i = 0; i < g_pcTrail; ++i)
				out[i] = m_pcTrail[(pos + i) % g_pcTrail].load(std::memory_order_relaxed);
			return out;	// oldest first
		}
		// Panel knobs: ADC value for each multiplexer code (what the OS writes to $202000)
		void setAdc(uint8_t _value) { m_adc.fill(_value); }
		void setAdc(uint8_t _select, uint8_t _value) { m_adc[_select] = _value; }
		uint32_t getSR() const;
	private:
		uint32_t m_sciDataWrites = 0;
		uint32_t m_romWrites = 0;
	};
}
