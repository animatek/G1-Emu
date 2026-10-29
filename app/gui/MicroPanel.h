#pragma once

// The panel of the Micro Modular model, laid out as the box's top: the model's name, and in front,
// left to right, Shift, Volume, the edit knobs 1-3 (with what Shift makes of them), the two-digit
// 7-segment display and the two buttons on the right. It reads the display the OS drives
// (Microcontroller::microDigit) and writes the knobs the OS reads through the QSPI (setQspiAdc) and
// the buttons it reads on port F (setMicroButton). Shift is also the computer's Shift key, as on the
// G1's panel.

#include "Overlay.h"
#include "Panel.h"
#include "SynthSettingsView.h"

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

	// One of the box's buttons: held while the mouse is down on it (or, for Shift, while the
	// computer's Shift key is).
	class MicroButton : public juce::Component, public juce::SettableTooltipClient
	{
	public:
		std::function<void(bool)> onHeld;
		void setKeyHeld(bool _held) { m_key = _held; update(); }
		void paint(juce::Graphics& _g) override;
		void mouseDown(const juce::MouseEvent&) override { m_mouse = true; update(); }
		void mouseUp(const juce::MouseEvent&) override { m_mouse = false; update(); }
	private:
		void update();
		bool m_mouse = false, m_key = false, m_held = false;
	};

	class MicroPanel : public juce::Component, private juce::Timer
	{
	public:
		// The box's top, about 1.68 to 1, and below it the G1's bars: the status bar (the emulator's
		// figures; Extras, Patreon, Report issue and Settings) and the extras tray, which the
		// Extras arrow shows or hides as on the G1 (Presets, the tooltips, About, the power switch).
		static constexpr int Width = 960, BoxHeight = 572, StatusHeight = 36, ExtrasHeight = 60;
		static constexpr int Height = BoxHeight + StatusHeight + ExtrasHeight;	// with the tray open

		// The height it wants now (the tray open or not), and a call when that changes, for the
		// window or the plugin's editor to follow.
		int preferredHeight() const { return BoxHeight + StatusHeight + (m_extrasOpen ? ExtrasHeight : 0); }
		std::function<void()> onHeightChanged;

		explicit MicroPanel(PanelHost& _host);
		~MicroPanel() override;

		void paint(juce::Graphics& _g) override;
		void resized() override;

		// The window's host keeps the volume where it was left.
		int volume() const { return m_knobs[0].value(); }

	private:
		void timerCallback() override;
		void drawDigit(juce::Graphics& _g, juce::Rectangle<float> _r, uint8_t _segments) const;
		juce::Rectangle<int> presetsFrame() const;

		PanelHost& m_host;
		std::array<MicroKnob, 4> m_knobs;	// volume, then 1-3
		std::array<MicroButton, 3> m_buttons;	// Shift, and the two on the right
		DelayedTooltips m_tooltips{this};
		PresetsView m_presets;		// the Micro's one bank, loading into its one slot
		juce::TextButton m_presetsButton{"Presets"}, m_about{"About"};
		juce::ToggleButton m_tipsToggle{"Tooltips"};
		IconButton m_patreon{"Patreon", IconButton::Icon::Patreon}, m_report{"Report", IconButton::Icon::Report};
		IconButton m_power{"Restart", IconButton::Icon::Restart};
		ExtrasDrawer m_drawer;		// the tray, painted as the G1's
		AboutView m_aboutView;
		double m_peakHold = 0;
		IconButton m_settings{"Settings", IconButton::Icon::Settings};	// the emulator's, on a card as on the G1
		IconButton m_extras{"Extras", IconButton::Icon::ExtrasOpen};
		CardView m_settingsCard;
		bool m_extrasOpen = true;
		void setExtrasOpen(bool _open);
		juce::Label m_status;
		bool m_osModified = false;	// the OS is not Clavia's as it came (g1osversion.h): the MOD sticker
		std::array<uint8_t, 2> m_digits{{0xff, 0xff}};	// as the OS drives them: a segment is lit when its bit is low
	};
}
