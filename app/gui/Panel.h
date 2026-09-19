#pragma once

// El panel del G1 emulado: la pantalla, los 18 mandos y el volumen, los botones y los LEDs, y
// una barra de estado con la velocidad y la carga. Lee y escribe el panel del emulador
// (Microcontroller: getLcd, ledRow, setButton, setAdc), que se puede tocar desde este hilo.

#include "emuhost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace g1gui
{
	// Donde esta cada cosa en las matrices del panel (ver NOTAS.md, «El panel»).
	struct ButtonBit { int row = -1, bit = -1; bool known() const { return row >= 0; } };
	struct LedBit { int row = -1, bit = -1; bool known() const { return row >= 0; } };

	class LcdView : public juce::Component
	{
	public:
		explicit LcdView(const g1::Lcd& _lcd) : m_lcd(_lcd) {}
		void paint(juce::Graphics& _g) override;
	private:
		const g1::Lcd& m_lcd;
	};

	class LedView : public juce::Component
	{
	public:
		void setOn(bool _on) { if(_on != m_on) { m_on = _on; repaint(); } }
		void paint(juce::Graphics& _g) override;
	private:
		bool m_on = false;
	};

	class PanelButton : public juce::TextButton
	{
	public:
		PanelButton(const juce::String& _name, g1::Microcontroller& _mc, ButtonBit _bit);
	private:
		g1::Microcontroller& m_mc;
		ButtonBit m_bit;
		bool m_down = false;
	};

	class Panel : public juce::Component, private juce::Timer
	{
	public:
		explicit Panel(g1app::EmuHost& _host);
		void paint(juce::Graphics& _g) override;
		void resized() override;

	private:
		void timerCallback() override;
		PanelButton& addButton(const juce::String& _name, ButtonBit _bit);
		LedView& addLed(LedBit _bit);

		g1app::EmuHost& m_host;
		g1::Microcontroller& m_mc;

		LcdView m_lcd;
		juce::Slider m_volume;
		std::array<juce::Slider, 18> m_knobs;
		std::array<LedView, 18> m_knobLeds;
		std::vector<std::unique_ptr<PanelButton>> m_buttons;
		std::vector<std::unique_ptr<LedView>> m_leds;
		std::vector<std::pair<LedView*, LedBit>> m_ledMap;

		// Botones con nombre, por si hay que colocarlos
		PanelButton* m_panelSplit = nullptr;
		PanelButton* m_find = nullptr;
		PanelButton* m_octDown = nullptr;
		PanelButton* m_octUp = nullptr;
		std::array<LedView*, 5> m_octLeds{};
		std::array<PanelButton*, 4> m_modeButtons{};	// Store, System, Edit, Patch/Load
		std::array<LedView*, 4> m_modeLeds{};
		std::array<PanelButton*, 4> m_slotButtons{};	// A-D
		std::array<LedView*, 4> m_slotLeds{};
		PanelButton* m_assign = nullptr;
		PanelButton* m_shift = nullptr;
		std::array<PanelButton*, 4> m_nav{};			// izquierda, arriba, abajo, derecha

		// Vista de matrices: los 24 botones y los 32 LEDs en crudo, para identificarlos.
		juce::ToggleButton m_showMatrix{"Matriz"};
		std::array<std::unique_ptr<PanelButton>, 24> m_rawButtons;
		std::array<LedView, 32> m_rawLeds;

		juce::Label m_status;
		double m_peakHold = 0;
	};
}
