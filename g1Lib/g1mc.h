#pragma once

// La CPU del Nord Modular G1: un Motorola 68331 (CPU32) emulado con el Musashi de
// Gearmulator, igual que la del Nord Lead 2X. Esta primera version solo sabe de
// ROM y RAM: todo lo demas (DSP, panel, flash, MIDI) se registra como "desconocido"
// para ir descubriendo el mapa de memoria real a partir de lo que toca el OS.

#include "mc68k/mc68k.h"

#include "g1dsp.h"
#include "g1flash.h"
#include "g1duart.h"

#include "mc68k/hdi08.h"
#include "hardwareLib/sciMidi.h"

#include <memory>

#include <array>
#include <cstdint>
#include <map>
#include <vector>

namespace g1
{
	// Mapa provisional: la pila del vector de reset apunta a $1FFF00, asi que se
	// supone ROM en $000000 (512 KB) y RAM hasta $1FFFFF. Se corrige cuando el
	// arranque programe los chip-selects.
	static constexpr uint32_t g_romSize = 0x80000;
	static constexpr uint32_t g_memSize = 0x200000;

	// Lo que programa el cargador en los chip-selects (ver NOTAS.md):
	static constexpr uint32_t g_ramAddress = 0x100000;		// CS8-10, 1 MB, 16 bits
	static constexpr uint32_t g_dspAddress = 0x200000;		// CS0, puertos HI08 de los DSP
	static constexpr uint32_t g_panelIn = 0x201800;			// CS4, lectura de la matriz de botones
	static constexpr uint32_t g_panelOut = 0x202000;		// CS2, filas de botones y LEDs
	static constexpr uint32_t g_panelAdc = 0x202800;		// CS3, ADC de los mandos (canal elegido en $202000)
	static constexpr uint8_t g_adcVolume = 0x30;			// codigo de multiplexor del volumen maestro
	static constexpr uint32_t g_flashAddress = 0x300000;	// CS7, 1 MB, 8 bits: el OS instalado
	static constexpr uint32_t g_flashSize = Flash::Size;

	// El OS de fabrica va dentro de la ROM, en $C800, compilado para correr en $100000.
	// El cargador lo copia a RAM si al encender se pulsa cierta combinacion; sin teclas,
	// copia el que haya en la flash de $300000 (longitud en +8, datos desde +$20).
	static constexpr uint32_t g_romOsOffset = 0xc800;
	static constexpr uint32_t g_romOsLongs = 0x1ce01;

	// 8 puertos HI08 en $200000 + 8*n: DSP 0-3 en la placa base, 4-7 en la expansion.
	static constexpr uint32_t g_hostPorts = 8;
	static constexpr uint32_t g_dspCount = 4;			// G1 sin tarjeta de expansion
	static constexpr uint32_t g_dspCyclesPerUcCycle = 6;	// aproximado: DSP ~100 MHz, CPU ~21 MHz
	static constexpr uint32_t g_ucClock = 20971520;			// SYNCR=$D300 con cristal de 32768 Hz
	static constexpr uint32_t g_sciRate = 44100;			// ritmo al que SciMidi mueve bytes
	static constexpr uint32_t g_ucCyclesPerSciSample = g_ucClock / g_sciRate;
	static constexpr uint32_t g_ucCyclesPerSerialByte = g_ucClock / 3125;	// 10 bits a 31250 baudios

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
		explicit Microcontroller(const std::vector<uint8_t>& _rom);

		uint32_t exec() override;
		uint16_t readImm16(uint32_t _addr) override;
		uint16_t read16(uint32_t _addr) override;
		uint8_t read8(uint32_t _addr) override;
		void write16(uint32_t _addr, uint16_t _val) override;
		void write8(uint32_t _addr, uint8_t _val) override;

		// Accesos fuera de ROM/RAM y de los perifericos internos, por direccion.
		const std::map<uint32_t, UnknownAccess>& unknownAccesses() const { return m_unknown; }
		uint32_t romWrites() const { return m_romWrites; }

		// Traza de los accesos a los puertos HI08 ($200000-$20003F), en orden.
		struct HostAccess { uint32_t addr; uint32_t value; uint32_t pc; bool write; uint8_t size; };
		std::vector<HostAccess>& hostTrace() { return m_hostTrace; }

		// Deja en la flash el OS de fabrica de la ROM, como lo dejaria una actualizacion.
		void installRomOsInFlash();
		Flash& getFlash() { return m_flash; }
		Dsp& getDsp(uint32_t _i) { return *m_dsps[_i]; }
		mc68k::Hdi08& getHostPort(uint32_t _i) { return m_hostPorts[_i]; }

		// La UART de la CPU (SCI del QSM, a 31250 baudios). Por ella habla el OS; falta
		// confirmar si es el PC PORT (editor) o el MIDI, porque el G1 tiene los dos.
		hwLib::SciMidi& getSci() { return m_sci; }
		uint32_t sciDataReads() const { return m_sciDataReads; }

		// PC PORT (el del editor): DUART en bus paralelo, ver g1duart.h.
		Duart& getPcPort() { return m_pcPort; }
		uint32_t pcPortIrqs() const { return m_pcPortIrqs; }
		uint32_t sciDataWrites() const { return m_sciDataWrites; }
		uint64_t ucCycles() const { return m_ucCycles; }

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

		std::vector<uint8_t> m_mem;		// ROM + RAM en un solo bloque
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
		// Temporizador periodico del SIM (PIT): el reloj del sistema del OS.
		uint16_t m_picr = 0, m_pitr = 0;
		std::array<uint8_t, 256> m_adc{};	// en el constructor: volumen al maximo, mandos a cero
		uint8_t m_adcSelect = 0;
		uint64_t m_pitAccum = 0;
		uint64_t m_pitIrqs = 0;
	public:
		uint64_t pitIrqs() const { return m_pitIrqs; }
		// Mandos del panel: valor del ADC para cada codigo de multiplexor (lo que el OS escribe en $202000)
		void setAdc(uint8_t _value) { m_adc.fill(_value); }
		void setAdc(uint8_t _select, uint8_t _value) { m_adc[_select] = _value; }
		uint32_t getSR() const;
	private:
		uint32_t m_sciDataWrites = 0;
		uint32_t m_romWrites = 0;
	};
}
