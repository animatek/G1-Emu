#pragma once

// The panel of the emulated G1, like the hardware's: the display, the volume and the 18 knobs
// with their LEDs, the buttons, and a status bar with speed and load. It reads and writes the
// emulator's panel (Microcontroller: getLcd, ledRow, setButton, setAdc), which can be used from
// this thread. Where every button and LED sits in the matrices: see NOTES.md, "The panel".

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

	// A panel button: while pressed, its matrix bit is 1. Without a known bit it is drawn
	// greyed out and does nothing.
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

	// The dial: the rotary encoder to the right of the display. Dragging it up and down or
	// using the wheel turns it, and the emulator hands the OS its quadrature edges.
	class DialView : public juce::Component, public juce::SettableTooltipClient
	{
	public:
		explicit DialView(g1::Microcontroller& _mc) : m_mc(_mc) { setTooltip("Dial"); }
		void paint(juce::Graphics& _g) override;
		void mouseDown(const juce::MouseEvent& _e) override { m_lastY = _e.y; }
		void mouseDrag(const juce::MouseEvent& _e) override;
		void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& _w) override;
	private:
		void turn(int _detents);
		g1::Microcontroller& m_mc;
		int m_lastY = 0;
		float m_angle = 0;
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
		std::array<PanelButton*, 4> m_nav{};			// up, left, right, down
		DialView m_dial;

		juce::Label m_status;
		juce::TextButton m_settings{"Settings"};
		double m_peakHold = 0;
		uint64_t m_lastMidiIn = 0;
		int m_midiHold = 0;
	};
}
