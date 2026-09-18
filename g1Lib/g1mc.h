#pragma once

// La CPU del Nord Modular G1: un Motorola 68331 (CPU32) emulado con el Musashi de
// Gearmulator, igual que la del Nord Lead 2X. Esta primera version solo sabe de
// ROM y RAM: todo lo demas (DSP, panel, flash, MIDI) se registra como "desconocido"
// para ir descubriendo el mapa de memoria real a partir de lo que toca el OS.

#include "mc68k/mc68k.h"

#include "g1flash.h"

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
	static constexpr uint32_t g_flashAddress = 0x300000;	// CS7, 1 MB, 8 bits: el OS instalado
	static constexpr uint32_t g_flashSize = Flash::Size;

	// El OS de fabrica va dentro de la ROM, en $C800, compilado para correr en $100000.
	// El cargador lo copia a RAM si al encender se pulsa cierta combinacion; sin teclas,
	// copia el que haya en la flash de $300000 (longitud en +8, datos desde +$20).
	static constexpr uint32_t g_romOsOffset = 0xc800;
	static constexpr uint32_t g_romOsLongs = 0x1ce01;

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

		uint16_t readImm16(uint32_t _addr) override;
		uint16_t read16(uint32_t _addr) override;
		uint8_t read8(uint32_t _addr) override;
		void write16(uint32_t _addr, uint16_t _val) override;
		void write8(uint32_t _addr, uint8_t _val) override;

		// Accesos fuera de ROM/RAM y de los perifericos internos, por direccion.
		const std::map<uint32_t, UnknownAccess>& unknownAccesses() const { return m_unknown; }
		uint32_t romWrites() const { return m_romWrites; }

		// Deja en la flash el OS de fabrica de la ROM, como lo dejaria una actualizacion.
		void installRomOsInFlash();
		Flash& getFlash() { return m_flash; }

	private:
		bool isInternalPeripheral(uint32_t _addr) const { return (_addr & 0xfff000) == 0xfff000; }
		void logUnknown(uint32_t _addr, bool _write, uint32_t _value);

		std::vector<uint8_t> m_mem;		// ROM + RAM en un solo bloque
		Flash m_flash;					// $300000
		std::map<uint32_t, UnknownAccess> m_unknown;
		uint32_t m_romWrites = 0;
	};
}
