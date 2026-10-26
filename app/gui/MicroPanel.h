#pragma once

// The panel of the Micro Modular model: the red box with its four knobs (volume and the three
// edit knobs) and the two-digit 7-segment display, on one DSP and its own OS. It reads the display
// the OS drives (Microcontroller::microDigit) and writes the knobs the OS reads through the QSPI
// (setQspiAdc). The buttons are not here yet: where they sit on the 68331's port F is not mapped
// (NOTES.md, "Micro Modular in G1-Emu").

#include "Panel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace g1gui
{
	// A rotary knob dragged up and down, 0-255, double-click to go back to its default.
	class MicroKnob : public juce::Component, public juce::SettableTooltipClient
	{
	public:
		MicroKnob(const juce::String& _label, int _default);
		int value() const { return m_value; }
		void setValue(int _v, bool _notify);
		std::function<void(int)> onChange;

		void paint(juce::Graphics& _g) override;
		void mouseDown(const juce::MouseEvent& _e) override;
		void mouseDrag(const juce::MouseEvent& _e) override;
		void mouseDoubleClick(const juce::MouseEvent&) override { setValue(m_default, true); }
		void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& _w) override;

	private:
		juce::String m_label;
		int m_value = 0, m_default = 0, m_dragStart = 0;
	};

	class MicroPanel : public juce::Component, private juce::Timer
	{
	public:
		static constexpr int Width = 640, Height = 360;

		explicit MicroPanel(PanelHost& _host);
		~MicroPanel() override;

		void paint(juce::Graphics& _g) override;
		void resized() override;

		// The window's host keeps the volume where it was left.
		int volume() const { return m_knobs[0].value(); }

	private:
		void timerCallback() override;
		void drawDigit(juce::Graphics& _g, juce::Rectangle<float> _r, uint8_t _segments) const;

		PanelHost& m_host;
		std::array<MicroKnob, 4> m_knobs;	// volume, then 1-3
		juce::TextButton m_settings{"Settings"};
		juce::Label m_status;
		std::array<uint8_t, 2> m_digits{{0xff, 0xff}};	// as the OS drives them: a segment is lit when its bit is low
	};
}
