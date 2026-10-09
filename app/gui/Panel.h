#pragma once

// The panel of the emulated G1, like the hardware's: the display, the volume and the 18 knobs
// with their LEDs, the buttons, and a status bar with speed and load. It reads and writes the
// emulator's panel (Microcontroller: getLcd, ledRow, setButton, setAdc), which can be used from
// this thread. Where every button and LED sits in the matrices: see NOTES.md, "The panel".
//
// The same panel is the window of g1gui and the editor of the plugin: what it needs from
// whoever runs the G1 is PanelHost.

#include "hostconfig.h"
#include "g1Lib/g1knobs.h"
#include "SynthSettingsView.h"
#include "Overlay.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <utility>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace g1gui
{
	// The notice about Clavia, ROMs and support. It is the same text as the README's "Please read
	// this first", shown in the settings window and, the first time G1-Emu runs, at startup; the plugin shows it
	// in its Settings.
	const char* disclaimerText();

	struct MatrixBit { int row = -1, bit = -1; bool known() const { return row >= 0; } };

	// What the panel needs from whoever runs the G1: the G1 itself, the figures for the status
	// bar, where to keep the panel's own preferences, and what its Settings button opens.
	class PanelHost
	{
	public:
		virtual ~PanelHost() = default;
		virtual g1::Microcontroller& mc() = 0;
		virtual g1app::HostStats stats() = 0;
		virtual bool extrasOpen() const = 0;
		virtual void setExtrasOpen(bool _open) = 0;
		virtual bool knobDisplays() const = 0;
		virtual void setKnobDisplays(bool _on) = 0;
		virtual bool knobFollowsPatch() const = 0;		// a knob shows its parameter's value, not where it was turned
		virtual void setKnobFollowsPatch(bool _on) = 0;
		virtual uint32_t randomExcluded() const = 0;	// the knobs Random leaves alone: bit k for knob k + 1
		virtual void setRandomExcluded(uint32_t _knobs) = 0;
		// The tooltips beside the mouse, on unless turned off in the extras. Where the host does not
		// keep the choice, it is on at every start.
		virtual bool tooltips() const { return true; }
		virtual void setTooltips(bool) {}
		// The Presets page's Hide empty, on unless turned off. Where the host does not keep the
		// choice, it is on at every start.
		virtual bool presetsHideEmpty() const { return true; }
		virtual void setPresetsHideEmpty(bool) {}
		virtual float panelScale() const = 0;			// the window's size, as PanelView keeps it
		virtual void setPanelScale(float _scale) = 0;
		virtual juce::String settingsTooltip() const = 0;
		virtual void showSettings(juce::Component* _parent) = 0;
		virtual g1app::SynthSettingsLink& synthSettings() = 0;	// the OS's, for the extras' overlay
		virtual g1app::PresetsLink& presets() = 0;				// the OS's banks, for the Presets page
		// Switching the G1 off and on, where the host can: the panel shows its Restart button only
		// then. The host deletes this panel and makes a new one, so it must do it later, not inside
		// the call.
		virtual bool canRestart() const { return false; }
		virtual void restart() {}
		virtual juce::String restartNote() const { return {}; }	// what a restart keeps and loses, for the question
	};

	// The panel is drawn from the PNGs in skin/ (built in as G1Skin), laid out in the background's
	// own pixels (3000 x 1238) and shown at SkinScale of them.
	struct Skin;

	// What the tooltips' display below the knobs says about a control of the synth's own: its name,
	// and its value after a tab. Its hover tooltip only hints at how to use it (right click: hold it
	// down); what is not on the hardware has a hover tooltip alone.
	void setLcdTip(juce::Component& _c, const juce::String& _tip);

	// The display's glass: the red frame around it is the background's.
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
		LedView() { setInterceptsMouseClicks(false, false); }
		void setOn(bool _on) { if(_on != m_on) { m_on = _on; repaint(); } }
		void paint(juce::Graphics& _g) override;
	private:
		bool m_on = false;
	};

	// A panel button: while pressed, its matrix bit is 1. Without a known bit it is drawn
	// greyed out and does nothing. A mouse presses one button at a time, so a button can also be
	// held without it: from the computer's keyboard (Shift, A-D), or latched with a right click
	// (another right click lets it go). A held button is drawn down and lit. Its bounds are the
	// whole sprite, shadow included; only the button itself takes the mouse.
	class PanelButton : public juce::Button, private juce::Timer
	{
	public:
		enum class Shape { Wide, Tall, Tilted };	// the navigator's up/down are tall, Assign/Morph tilted
		PanelButton(const juce::String& _name, g1::Microcontroller& _mc, MatrixBit _bit, Shape _shape = Shape::Wide);
		void paintButton(juce::Graphics& _g, bool _over, bool _down) override;
		bool hitTest(int _x, int _y) override;
		void setKeyHeld(bool _held) { if(_held != m_keyHeld) { m_keyHeld = _held; update(); } }
		// Held with the mouse, it presses again and again after a moment, as a key that repeats:
		// the G1 takes a held key as one press (the navigator's). Such a key does not latch.
		void setAutoRepeat(bool _on);
		// A key whose being held does nothing (Panel Split toggles when pressed): no latching.
		void setLatchable(bool _on);
		bool isLatchable() const { return m_latchable; }	// a right click latches it: no menu
		void mouseDown(const juce::MouseEvent& _e) override;
		void mouseDrag(const juce::MouseEvent& _e) override;
		void mouseUp(const juce::MouseEvent& _e) override;
		bool isPressed() const { return m_down; }	// as the G1 sees it: by the mouse, a key or latched
		void unlatch() { if(m_latched) { m_latched = false; update(); } }
		// Still down: the G1 sees it let go for _upMs and pressed again. Called again meanwhile,
		// the last one decides when it goes down.
		void pressAgain(juce::uint32 _upMs);
		bool wasTaken() const { return m_taken; }	// this press was the window's (takesPress)
		std::function<void()> onPress, onRelease;	// each time it goes down, and up
		// Asked as it goes down: true keeps the press for the window, and the G1 never sees it.
		std::function<bool()> takesPress;
	private:
		void update();
		void timerCallback() override;
		void repeat();	// a navigator key held with the mouse: let go and pressed again in turns
		void setBit(bool _down);
		bool held() const { return m_keyHeld || m_latched; }
		bool repeating() const { return m_autoRepeat && m_down && isDown(); }
		bool timerNeeded() const { return repeating() || held(); }
		g1::Microcontroller& m_mc;
		MatrixBit m_bit;
		Shape m_shape;
		bool m_down = false;
		bool m_keyHeld = false, m_latched = false;
		bool m_autoRepeat = false, m_repeatUp = false;	// m_repeatUp: let go for a moment between presses
		bool m_wasHeld = false;
		bool m_latchable = true;
		bool m_unlatching = false;	// this left click lets a latched button go, and presses nothing
		bool m_taken = false;		// this press is the window's (takesPress)
		int m_pressAgainId = 0;		// the latest pressAgain
		juce::uint32 m_pressedAt = 0;
	};

	// The dial: the rotary encoder to the right of the display. Dragging it up and down or
	// using the wheel turns it, and the emulator hands the OS its quadrature edges.
	class DialView : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
	{
	public:
		explicit DialView(g1::Microcontroller& _mc) : m_mc(_mc) { setLcdTip(*this, "Data Wheel"); }
		void paint(juce::Graphics& _g) override;
		bool hitTest(int _x, int _y) override;
		void mouseDown(const juce::MouseEvent& _e) override { m_lastY = _e.y; }
		void mouseDrag(const juce::MouseEvent& _e) override;
		void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& _w) override;
	private:
		void turn(int _detents);
		void timerCallback() override;
		g1::Microcontroller& m_mc;
		int m_lastY = 0;
		// Degrees turned, clockwise from the indent straight up, shown and asked for: the picture
		// follows. It starts with the indent at about eight o'clock, where Mike Fiction likes it.
		static constexpr float StartAngle = 255.0f;
		float m_angle = StartAngle, m_targetAngle = StartAngle;
	};

	// A button of the window's own, not the G1's, drawn as the panel's wide ones: Main and Synth
	// Settings under the tooltips' display, as Mike Fiction's Nord Lead 2x skin has its page buttons.
	class PageButton : public juce::Button
	{
	public:
		explicit PageButton(const juce::String& _name) : juce::Button(_name) {}
		void paintButton(juce::Graphics& _g, bool _over, bool _down) override;
		bool hitTest(int _x, int _y) override;
	};

	// A small square button with an icon instead of text, for what is not on the hardware
	// (Settings, Report issue, Patreon). The tooltip says what it does.
	class IconButton : public juce::Button
	{
	public:
		enum class Icon { Settings, Report, Patreon, ExtrasOpen, ExtrasClose, Restart };
		IconButton(const juce::String& _name, Icon _icon) : juce::Button(_name), m_icon(_icon) {}
		void setIcon(Icon _icon) { m_icon = _icon; repaint(); }
		void paintButton(juce::Graphics& _g, bool _over, bool _down) override;
	private:
		Icon m_icon;
	};

	// A small display above a knob, in the colours of the G1's own: the module and value on
	// top, the parameter below, as the OS names them. Blank when the knob has nothing.
	class KnobDisplay : public juce::Component, public juce::SettableTooltipClient
	{
	public:
		void set(const g1::KnobInfo& _info, bool _hz);	// _hz: a pitch with a Hz reading shows it
		void paint(juce::Graphics& _g) override;
		void mouseUp(const juce::MouseEvent& _e) override;
		std::function<void()> onClick;	// a click on a pitch that reads as a note or in Hz
	private:
		juce::String m_top, m_bottom;
		bool m_assigned = false, m_switchable = false;
	};

	// A button that also answers a double click (Random: back to the patch's values). The double
	// click arrives on the second press, and its release would still be a click: that one is eaten.
	class DoubleClickButton : public juce::TextButton
	{
	public:
		using juce::TextButton::TextButton;
		std::function<void()> onSingleClick, onDoubleClick;
		void mouseDoubleClick(const juce::MouseEvent&) override
		{
			m_eatClick = true;
			if(onDoubleClick)
				onDoubleClick();
		}
	protected:
		void clicked() override
		{
			if(std::exchange(m_eatClick, false))
				return;
			if(onSingleClick)
				onSingleClick();
		}
	private:
		bool m_eatClick = false;
	};

	// The extras drawer's tray, with what it holds as children. It slides inside a clip below the
	// status bar, so it comes out from under it instead of appearing.
	class ExtrasDrawer : public juce::Component
	{
	public:
		void paint(juce::Graphics& _g) override;
	};

	class KnobLook : public juce::LookAndFeel_V4
	{
	public:
		void drawRotarySlider(juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
	};

	// JUCE's tooltip window shows the next tip at once for a while after one has gone; this one
	// waits the same each time: a control's tip comes only once the mouse has rested on it for
	// DelayMs. And none at all when turned off (the extras).
	class DelayedTooltips : public juce::TooltipWindow
	{
	public:
		static constexpr juce::uint32 DelayMs = 500;
		explicit DelayedTooltips(juce::Component* _parent) : juce::TooltipWindow(_parent, 0) {}
		juce::String getTipFor(juce::Component& _c) override;
		bool enabled = true;
	private:
		const juce::Component* m_over = nullptr;	// only compared, never used: it may be gone
		juce::uint32 m_overSince = 0;
	};

	// The tooltips of what is not on the hardware, and the hints of what is (right click: hold
	// it down): on a dark glass that blurs what is behind it.
	// _window is the tooltip window it draws, a child of the panel: what is behind it is the panel.
	class TooltipLook : public juce::LookAndFeel_V4
	{
	public:
		explicit TooltipLook(juce::Component& _window) : m_window(_window) {}
		juce::Rectangle<int> getTooltipBounds(const juce::String& _tip, juce::Point<int> _pos, juce::Rectangle<int> _parentArea) override;
		void drawTooltip(juce::Graphics& _g, const juce::String& _tip, int _w, int _h) override;
	private:
		juce::Component& m_window;
		bool m_snapshotting = false;	// while the panel behind is drawn for the blur, the tooltip is not
	};

	// The right-click menus, on the tooltips' glass: what is behind them blurred and darkened, a
	// lighter band under the mouse, thin rules between groups. They open inside the panel, so
	// they scale with it; capture() takes the picture of the panel they blur, before one opens.
	class MenuLook : public juce::LookAndFeel_V4
	{
	public:
		explicit MenuLook(juce::Component& _panel);
		~MenuLook() override;
		void capture();

		void preparePopupMenuWindow(juce::Component& _window) override;
		void drawPopupMenuBackgroundWithOptions(juce::Graphics&, int, int, const juce::PopupMenu::Options&) override {}
		void drawPopupMenuItemWithOptions(juce::Graphics& _g, const juce::Rectangle<int>& _area, bool _highlighted,
			const juce::PopupMenu::Item& _item, const juce::PopupMenu::Options&) override;
		void drawPopupMenuSectionHeaderWithOptions(juce::Graphics& _g, const juce::Rectangle<int>& _area,
			const juce::String& _name, const juce::PopupMenu::Options&) override;
		void getIdealPopupMenuItemSizeWithOptions(const juce::String& _text, bool _separator, int _standardHeight,
			int& _w, int& _h, const juce::PopupMenu::Options&) override;
		void getIdealPopupMenuSectionHeaderSizeWithOptions(const juce::String& _text, int _standardHeight, int& _w, int& _h,
			const juce::PopupMenu::Options& _options) override;
		int getPopupMenuBorderSizeWithOptions(const juce::PopupMenu::Options&) override { return 0; }
		juce::Font getPopupMenuFont() override;
	private:
		class Glass;
		juce::Component& m_panel;
		juce::Image m_behind;	// the panel as it was when the menu opened, in its pixels on the screen
		float m_behindScale = 1.0f;
		std::vector<std::unique_ptr<Glass>> m_glasses;	// one per menu window; dropped once it closes
	};

	// The display below the knobs, in the knob displays' dots: the name of the synth's control
	// under the mouse (setLcdTip), and its value at the right, after a tab. What is too long for it
	// scrolls through and starts over. When the mouse leaves the control, its name stays a moment
	// (LingerMs) and fades out quickly (FadeMs); another control's name comes at once, never faded in.
	class TipDisplay : public juce::Component, private juce::Timer
	{
	public:
		static constexpr double LingerMs = 500.0, FadeMs = 60.0;
		TipDisplay() { setInterceptsMouseClicks(false, false); }
		void set(const juce::String& _tip);
		void flash(const juce::String& _text);	// shown twice, blinking, over the tip (Shift + Find: Panic)
		void paint(juce::Graphics& _g) override;
	private:
		void timerCallback() override;	// a step of the flash or of the scrolling
		void updateTimer();
		void leave(double _now);		// each frame after the mouse left: linger, then fade
		bool scrolls() const;
		juce::String line() const;		// what shows now
		juce::String m_tip, m_text, m_value, m_flash;	// m_text and m_value stay while they linger
		int m_scroll = 0, m_ticks = 0;
		int m_flashSteps = 0;			// left of the flash
		float m_shown = 1.0f;			// how visible the name is: below 1 while it fades out
		double m_leftAt = -1.0;			// when the mouse left, in VBlank time; -1 before the next frame
		std::optional<juce::VBlankAttachment> m_leaving;	// while it lingers and fades
	};

	class Panel : public juce::Component, private juce::Timer
	{
	public:
		static constexpr float SkinScale = 0.4f;	// of the background's pixels: 3000 x 1238 shown as 1200 x 495
		static constexpr int Width = 1200, FaceHeight = 495, Height = FaceHeight + 36;	// the status bar below the face
		static constexpr juce::uint32 FaceColour = 0xff35263d;	// the skin's dark purple, for what is around it
		static constexpr uint8_t VolumeAdc = 0x30;				// the master volume's knob

		explicit Panel(PanelHost& _host);
		~Panel() override;
		void paint(juce::Graphics& _g) override;
		void resized() override;
		bool keyPressed(const juce::KeyPress& _key) override;
		bool keyStateChanged(bool _isKeyDown) override;
		void mouseDown(const juce::MouseEvent& _e) override;
		void focusLost(FocusChangeType) override { updateHeldKeys(); }

	private:
		void timerCallback() override;
		void updateKnobs(const std::array<g1::KnobInfo, 18>& _info);
		void turnKnobs(double _now);		// one frame of the knobs turning to their targets
		void startKnobTurn();				// turnKnobs on each frame, until the knobs are in place
		void setKnobTurning(size_t _knob, int _position);	// Random and its reset: set at once, drawn turning
		void updateLcdTips(const std::array<g1::KnobInfo, 18>& _info);
		void updateStatus();	// the status bar and the MIDI LED
		void updateHeldKeys();
		PanelButton& addButton(const juce::String& _name, MatrixBit _bit, PanelButton::Shape _shape = PanelButton::Shape::Wide);
		LedView& addLed(MatrixBit _bit);
		void reportIssue();
		void setExtrasOpen(bool _open, bool _animate = false);
		void slideDrawer(double _now);
		void placeDrawer();
		void shiftAsideForKnobs(std::function<void()> _turn);	// Shift let go for the OS, then _turn
		void randomizeKnobs();
		void showKnobMenu(size_t _knob);	// right click: Exclude from Random, or Include
		void showPanelMenu();				// right click anywhere else: GUI Scale, Settings, About
		bool hasOwnRightClick(const juce::Component* _c) const;	// a control that answers a right click itself
		void showMenu(juce::PopupMenu& _menu);		// on the glass, at the mouse
		void updateRandomExcluded();		// the knobs' marks and hover tooltips, from the host
		void restoreKnobs();
		void setKnobDisplays(bool _on);
		void updateTip();
		void showSynthSettings(bool _show);	// the Synth Settings page, or the main one
		void showPresets(bool _show);		// the Presets page, or the main one
		int activeSlot();					// 0-3, the active slot, read from the OS

		juce::SharedResourcePointer<Skin> m_skin;	// keeps the images while a panel is open
		PanelHost& m_host;
		g1::Microcontroller& m_mc;
		KnobLook m_knobLook;

		LcdView m_lcd;
		juce::Slider m_volume;
		std::array<juce::Slider, 18> m_knobs;
		std::array<juce::uint32, 18> m_knobTurnedAt{};	// when the window last turned each (0: never)
		std::array<double, 18> m_knobTarget{};			// where each knob is going (updateKnobs)
		std::optional<juce::VBlankAttachment> m_knobTurn;	// while a knob turns into place
		double m_knobTurnLast = -1.0;					// the last frame's time, while they turn
		std::array<LedView*, 18> m_knobLeds{};
		std::vector<std::unique_ptr<PanelButton>> m_buttons;
		std::vector<std::unique_ptr<LedView>> m_leds;
		std::vector<std::pair<LedView*, MatrixBit>> m_ledMap;

		LedView* m_midiLed = nullptr;
		LedView* m_panelSplitLed = nullptr;
		PanelButton* m_panelSplit = nullptr;
		PanelButton* m_find = nullptr;
		std::array<PanelButton*, 4> m_modeButtons{};	// Store, System, Edit, Patch/Load
		std::array<LedView*, 4> m_modeLeds{};
		std::array<PanelButton*, 4> m_slotButtons{};	// A-D
		std::array<LedView*, 4> m_slotLeds{};
		PanelButton* m_assign = nullptr;
		PanelButton* m_shift = nullptr;
		// The buttons with a second function under Shift, printed red under them: the display
		// names what the button does now, with Shift down or not, and flashes it when it is pressed
		// with Shift (buttonPressed). Blank where the OS ignores it (Store in Edit mode).
		struct ShiftName { PanelButton* button; std::function<juce::String(bool _shift)> name; };
		std::vector<ShiftName> m_shiftNames;
		// The G1's mode, from its mode LEDs: what Store can do depends on it.
		enum class Mode { Other, Patch, System, Edit };
		Mode m_mode = Mode::Other;
		std::array<juce::uint32, 4> m_modeLitAt{};	// when each mode LED was last seen lit
		void updateMode();
		juce::String storeName(bool _shift) const;
		juce::String shiftedKnobTip(const g1::KnobInfo& _k) const;	// what a knob does with Shift down
		bool showsHz(const g1::KnobInfo& _k) const;				// its pitch in Hz, not as a note
		static std::array<uint8_t, 4> pitchKey(const g1::KnobInfo& _k);
		bool ledLit(MatrixBit _bit) const;
		bool m_shiftKey = false;	// Shift on the computer's keyboard, held
		// Shift is for the next key only, as on the hardware: the OS sees it no more past that key
		// (and Shift + Store goes on to Store?). So it lets go once that key does; the keyboard's
		// Shift is spent until it is pressed again. Not after a slot button: Shift stays down for
		// the next slot, pressed again for the OS (buttonReleased). Nor after what the window takes
		// for itself (Shift + Patch/Load: Random), which can be pressed again and again.
		bool m_shiftUsed = false, m_shiftSpent = false;
		void buttonPressed(PanelButton& _b);
		void buttonReleased(PanelButton& _b);
		std::array<PanelButton*, 4> m_nav{};			// up, left, right, down
		DialView m_dial;

		juce::Label m_status;
		IconButton m_settings{"Settings", IconButton::Icon::Settings};
		IconButton m_report{"Report issue", IconButton::Icon::Report};
		IconButton m_patreon{"Patreon", IconButton::Icon::Patreon};
		IconButton m_extras{"Extras", IconButton::Icon::ExtrasOpen};

		// The extras drawer below the panel: what the hardware never had. Closed, the window is
		// the panel alone.
		bool m_extrasOpen = false;
		juce::Component m_drawerClip;
		ExtrasDrawer m_drawer;
		float m_drawerShown = 0.0f;					// 0 closed, 1 open
		float m_slideFrom = 0.0f;
		double m_slideStart = -1.0;
		std::optional<juce::VBlankAttachment> m_slide;	// while it slides
		DoubleClickButton m_random{"Random"};
		juce::ToggleButton m_tipsToggle{"Tooltips"};
		juce::ToggleButton m_displaysToggle{"Parameter Displays"};
		juce::ToggleButton m_followToggle{"Knob Follows Patch"};
		IconButton m_restart{"Restart", IconButton::Icon::Restart};	// where the host can (canRestart)
		juce::TextButton m_about{"About"};		// the credits, the licenses and Animatek NME (AboutView)
		// The pages over the knobs, each button a switch with its LED lit while its page shows; with
		// neither, the synth's panel.
		PageButton m_presetsPage{"Presets"}, m_settingsPage{"Settings"};
		LedView m_presetsLed, m_settingsLed;
		SynthSettingsView m_synthView;	// over the knobs while open
		PresetsView m_presetsView;		// the same place
		ConfirmView m_confirm;			// the same, for a question (Restart)
		AboutView m_aboutView;			// and for About
		juce::Random m_rng;
		// A right click on a knob opens its menu; the slider still gets the click, which does not
		// turn it.
		struct KnobMenuListener : juce::MouseListener
		{
			explicit KnobMenuListener(Panel& _panel) : panel(_panel) {}
			void mouseDown(const juce::MouseEvent& _e) override;
			Panel& panel;
		};
		KnobMenuListener m_knobMenu{*this};

		// What each knob is assigned to, from the OS's tables (g1knobs.h), and the displays.
		g1::KnobMap m_knobMap;
		std::array<KnobDisplay, 18> m_knobDisplays;
		// The pitches shown in Hz after a click on their display (slot, section, module, type), until
		// the window closes. Not sent to the G1: its own display always reads them as notes.
		std::set<std::array<uint8_t, 4>> m_hzPitches;
		// The patch's values, taken before the first Random, so a double click can put them
		// back. Dropped when the knobs' assignments change (another patch).
		std::array<g1::KnobInfo, 18> m_snapshot{};
		bool m_haveSnapshot = false;
		TipDisplay m_tip;
		DelayedTooltips m_tooltips{this};
		TooltipLook m_tooltipLook{m_tooltips};
		MenuLook m_menuLook{*this};
		juce::Time m_menuClickAt;	// the right click that opened the panel's menu: the panel gets its own twice
		double m_peakHold = 0;
		uint64_t m_lastMidiIn = 0;
		int m_midiHold = 0;
	};

	// The panel at any size: it is laid out at Panel::Width and shown scaled to this view's width,
	// which keeps the panel's proportions (the window's resizer has to: aspectRatio). The scale is
	// the host's to keep. When the panel changes its own height (the extras drawer), the view
	// follows at the same scale and calls onAspectChanged.
	class PanelView : public juce::Component
	{
	public:
		static constexpr float MinScale = 0.5f, MaxScale = 2.5f;

		explicit PanelView(PanelHost& _host);
		double aspectRatio() const { return static_cast<double>(m_panel.getWidth()) / static_cast<double>(m_panel.getHeight()); }
		// The resizer's limits for these proportions, or _aspect's, from MinScale to MaxScale.
		void applyLimits(juce::ComponentBoundsConstrainer& _c) const { applyLimits(_c, aspectRatio()); }
		static void applyLimits(juce::ComponentBoundsConstrainer& _c, double _aspect);
		std::function<void()> onAspectChanged;
		float scale() const { return static_cast<float>(getWidth()) / static_cast<float>(m_panel.getWidth()); }
		void setScale(float _scale);	// the window follows: the standalone's fits it, the plugin's editor too

		void resized() override;
		void paint(juce::Graphics& _g) override;
		void childBoundsChanged(juce::Component* _child) override;

	private:
		PanelHost& m_host;
		Panel m_panel;
		int m_panelHeight = 0;	// the panel's own, to tell the drawer opening from a new scale
	};
}
