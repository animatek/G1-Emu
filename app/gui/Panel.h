#pragma once

// El panel del G1 emulado, como el del aparato: la pantalla, el volumen y los 18 mandos con sus
// LEDs, los botones y una barra de estado con la velocidad y la carga. Lee y escribe el panel
// del emulador (Microcontroller: getLcd, ledRow, setButton, setAdc), que se puede tocar desde
// este hilo. Donde esta cada boton y cada LED en las matrices: ver NOTAS.md, «El panel».

#include "emuhost.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

namespace g1gui
{
	struct MatrixBit { int row = -1, bit = -1; bool known() const { return row >= 0; } };

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

	// Un boton del panel: mientras se pulsa, su bit de la matriz esta a 1. Sin bit conocido,
	// se dibuja apagado y no hace nada.
	class PanelButton : public juce::Button
	{
	public:
		PanelButton(const juce::String& _name, g1::Microcontroller& _mc, MatrixBit _bit);
		void paintButton(juce::Graphics& _g, bool _over, bool _down) override;
	private:
		g1::Microcontroller& m_mc;
		MatrixBit m_bit;
		bool m_down = false;
	};

	class KnobLook : public juce::LookAndFeel_V4
	{
	public:
		void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
	};

	class Panel : public juce::Component, private juce::Timer
	{
	public:
		explicit Panel(g1app::EmuHost& _host);
		~Panel() override;
		void paint(juce::Graphics& _g) override;
		void resized() override;

	private:
		void timerCallback() override;
		PanelButton& addButton(const juce::String& _name, MatrixBit _bit);
		LedView& addLed(MatrixBit _bit);

		g1app::EmuHost& m_host;
		g1::Microcontroller& m_mc;
		KnobLook m_knobLook;

		LcdView m_lcd;
		juce::Slider m_volume;
		std::array<juce::Slider, 18> m_knobs;
		std::array<LedView*, 18> m_knobLeds{};
		std::vector<std::unique_ptr<PanelButton>> m_buttons;
		std::vector<std::unique_ptr<LedView>> m_leds;
		std::vector<std::pair<LedView*, MatrixBit>> m_ledMap;

		LedView* m_midiLed = nullptr;
		LedView* m_panelSplitLed = nullptr;
		PanelButton* m_panelSplit = nullptr;
		PanelButton* m_find = nullptr;
		std::array<PanelButton*, 2> m_oct{};
		std::array<LedView*, 5> m_octLeds{};
		std::array<PanelButton*, 4> m_modeButtons{};	// Store, System, Edit, Patch/Load
		std::array<LedView*, 4> m_modeLeds{};
		std::array<PanelButton*, 4> m_slotButtons{};	// A-D
		std::array<LedView*, 4> m_slotLeds{};
		PanelButton* m_assign = nullptr;
		PanelButton* m_shift = nullptr;
		std::array<PanelButton*, 4> m_nav{};			// arriba, izquierda, derecha, abajo
		juce::Rectangle<int> m_dial;

		juce::Label m_status;
		double m_peakHold = 0;
		uint64_t m_lastMidiIn = 0;
		int m_midiHold = 0;
	};
}
