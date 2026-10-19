#pragma once

// The synth settings over the panel (its Synth Settings page button): the slots' MIDI channels
// and the settings that apply to all of them, as the System menu and NME's Synth Settings dialog
// have them. Each change goes to the OS at once (SynthSettingsLink), which then reports what it
// took. It sits in Mike Fiction's frame (skin/settings_panel.png), over the knobs' four sections.

#include "presetslink.h"
#include "PchUpload.h"
#include "synthsettings.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <map>

namespace g1gui
{
	// One of the skin's pictures, or a sheet of square frames (_frameSize), scaled to the screen's
	// pixels where it is drawn: each frame once, kept until that size changes.
	class SkinImage
	{
	public:
		SkinImage(const void* _png, int _pngSize, int _frameSize = 0);
		void draw(juce::Graphics& _g, juce::Rectangle<float> _dest, int _frame = 0);
	private:
		juce::Image m_source;
		int m_frameSize;
		juce::Point<int> m_size;				// what m_frames are scaled to
		std::map<int, juce::Image> m_frames;
	};

	// The boxes and the name field as rounded boxes with white text, as Mike Fiction's
	// Nord Lead 2x settings have them.
	class SettingsLook : public juce::LookAndFeel_V4
	{
	public:
		SettingsLook();
		juce::Font getComboBoxFont(juce::ComboBox&) override;
		juce::Font getPopupMenuFont() override;
		void drawComboBox(juce::Graphics&, int _w, int _h, bool _down, int, int, int, int, juce::ComboBox&) override;
		void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
		void fillTextEditorBackground(juce::Graphics&, int _w, int _h, juce::TextEditor&) override;
		void drawTextEditorOutline(juce::Graphics&, int _w, int _h, juce::TextEditor&) override;
		// The panel's knob cap and its shadow.
		void drawRotarySlider(juce::Graphics&, int _x, int _y, int _w, int _h, float _pos, float, float, juce::Slider&) override;
		// Master tune's disk, ticks and name, round the knob centred at _centre.
		void drawTuneBackground(juce::Graphics& _g, juce::Point<float> _centre);

	private:
		SkinImage m_knob, m_shadow, m_tuneBg;
	};

	// A number in a box, for a range too long for a list (the clock's rate): dragged up or down,
	// turned with the wheel, or typed after a double click. Only digits go in, and what is typed is
	// brought into the range; Escape, or nothing typed, leaves the value as it was.
	class ValueBox : public juce::Component, public juce::SettableTooltipClient
	{
	public:
		ValueBox(int _min, int _max);
		void setValue(int _value);			// from outside: no onChange
		int getValue() const { return m_value; }
		std::function<void()> onChange;		// a change made here

		void paint(juce::Graphics& _g) override;
		void resized() override;
		void mouseDown(const juce::MouseEvent& _e) override;
		void mouseDrag(const juce::MouseEvent& _e) override;
		void mouseDoubleClick(const juce::MouseEvent&) override;
		void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& _wheel) override;

	private:
		void change(int _value);
		void commit();
		void cancel();

		const int m_min, m_max;
		int m_value, m_dragFrom = 0;
		juce::TextEditor m_editor;
	};

	class SynthSettingsView : public juce::Component, private juce::Timer
	{
	public:
		explicit SynthSettingsView(g1app::SynthSettingsLink& _link);
		~SynthSettingsView() override;

		// Shows it at _frame, in its parent's pixels (the knobs' sections); the rest of the panel
		// stays as it is and works. Faded in, on top. _under: the other page is fading in over it,
		// so it stays until covered.
		void open(juce::Rectangle<int> _frame);
		void close(bool _under = false);
		bool isOpen() const { return m_open; }	// still drawn for a moment after closing under the other page
		void octaveShiftChanged();	// set elsewhere (the panel's OCT SHIFT menu): the box shows it
		std::function<void()> onClose;
		// MIDI CC Mode and Octave Shift are the host's own settings, not the OS's: read on opening, set
		// at once.
		std::function<bool()> selectedControllers;
		std::function<void(bool)> setSelectedControllers;
		std::function<bool()> octaveShift;
		std::function<void(bool)> setOctaveShift;

		void paint(juce::Graphics& _g) override;
		void resized() override;
		bool keyPressed(const juce::KeyPress& _key) override;
		void mouseEnter(const juce::MouseEvent& _e) override;
		void mouseExit(const juce::MouseEvent& _e) override;

	private:
		void timerCallback() override;
		void repaintControl(const juce::MouseEvent& _e);	// the control under a mouse event, for its hover colour
		void show(const g1app::SynthSettings& _s);	// into the controls
		void apply();								// the controls to the OS
		void lcdTips();	// each control's name and value, for the panel's display

		g1app::SynthSettingsLink& m_link;
		SettingsLook m_look;
		g1app::SynthSettings m_known;	// what the OS said last: fields with no control keep theirs
		uint64_t m_revision = 0;
		int m_ticks = 0;				// the timer's, since it opened
		bool m_open = false;
		bool m_haveSettings = false;
		SkinImage m_frame;

		juce::Label m_nameLabel;
		juce::TextEditor m_name;
		std::array<juce::ComboBox, 4> m_channels;
		juce::ComboBox m_clock, m_sync;
		ValueBox m_bpm{24, 240};
		juce::Slider m_tune;			// a knob, its value on the panel's display (lcdTips)
		juce::ComboBox m_knob, m_pedal, m_program, m_leds;
		juce::ComboBox m_keyboardMode;		// Active Slot, or Selected Slots: the selected slots layered (MidiRouting)
		juce::Label m_keyboardModeLabel;	// the frame has no label for it yet: drawn as Name's is
		juce::ComboBox m_midiCc;			// Active Slot, or Selected Slots: controllers layered too (selectedControllers)
		juce::Label m_midiCcLabel;			// as Keyboard Mode's
		// A box like the others, but a click flips it: with two choices a list is one click too many.
		struct FlipBox : juce::ComboBox
		{
			// On the press alone: a ComboBox opens its list on the press and on the release too.
			void mouseDown(const juce::MouseEvent& _e) override { if(isEnabled() && !_e.mods.isPopupMenu()) flip(); }
			void mouseUp(const juce::MouseEvent&) override {}
			void mouseDrag(const juce::MouseEvent&) override {}
			void showPopup() override { flip(); }	// from the keyboard
			void flip() { setSelectedId(getSelectedId() == 1 ? 2 : 1); }
		};
		FlipBox m_octaveShift;				// Enabled, or Disabled: the notes as the rack plays them (octaveShift)
		juce::Label m_octaveShiftLabel;		// as Keyboard Mode's
		ValueBox m_velMin{0, 127}, m_velMax{0, 127};
		juce::Label m_note;
		juce::TextButton m_close{"Close"};
	};

	// The Presets page, in the same frame and place as the Synth Settings: one bank of the synth's
	// at a time, picked from a drop-down, its names in three columns (the empty positions hidden or
	// not), and a click that loads one into the active slot. The names come from the OS through
	// PresetsLink, read again each time the page opens or the bank changes.
	class PresetsView : public juce::Component, private juce::Timer
	{
	public:
		// _activeSlot says which slot (0-3) a click loads into: the one lit on the panel.
		PresetsView(g1app::PresetsLink& _link, std::function<int()> _activeSlot);
		~PresetsView() override;

		void open(juce::Rectangle<int> _frame);
		void close(bool _under = false);
		bool isOpen() const { return m_open; }	// still drawn for a moment after closing under the other page
		std::function<void()> onClose;

		// Reads the bank shown again: something may have been stored in it (the panel's Store).
		void refresh();

		// Hide empty, as the host keeps it: set at the start, and told each time it is clicked.
		void setHideEmpty(bool _on);
		std::function<void(bool)> onHideEmptyChanged;

		void paint(juce::Graphics& _g) override;
		void resized() override;
		bool keyPressed(const juce::KeyPress& _key) override;

		// Load .pch with this file, as if picked: it is read, and the page asks where it goes.
		void loadPch(const juce::File& _file);

	private:
		// The names, in columns top to bottom, as many rows as the bank needs; it scrolls.
		class List : public juce::Component
		{
		public:
			explicit List(PresetsView& _owner) : m_owner(_owner) {}
			void paint(juce::Graphics& _g) override;
			void mouseMove(const juce::MouseEvent& _e) override;
			void mouseExit(const juce::MouseEvent& _e) override;
			void mouseUp(const juce::MouseEvent& _e) override;
			int rowsFor(int _count) const;
		private:
			int positionAt(juce::Point<int> _p) const;	// -1: none
			PresetsView& m_owner;
			int m_hover = -1;
		};

		// Load .pch's question, over the list: which bank and position the patch goes to (each
		// position named after what it holds, the first empty one offered), a warning when that
		// one is taken, and Store, Load only (into the slot, stored nowhere) or Cancel.
		class StoreCard : public juce::Component
		{
		public:
			explicit StoreCard(PresetsView& _owner);
			void open(const juce::String& _name, int _bank);
			void refresh();					// the positions' names, once the bank is read
			void paint(juce::Graphics& _g) override;
			void resized() override;
		private:
			void fillPositions(bool _pickEmpty);
			void warn();
			PresetsView& m_owner;
			juce::String m_name;
			juce::Label m_bankLabel, m_posLabel, m_warning;
			juce::ComboBox m_bank, m_position;
			juce::TextButton m_store{"Store"}, m_loadOnly{"Load only"}, m_cancel{"Cancel"};
			bool m_known = false;
		};

		void timerCallback() override;
		void showBank();				// the link's names into the list
		void loadAt(int _position);
		void choosePch();
		void send(int _bank, int _position);	// m_pending, stored there (_bank < 0: only loaded)
		juce::String footer() const;

		g1app::PresetsLink& m_link;
		std::function<int()> m_activeSlot;
		SettingsLook m_look;
		SkinImage m_frame;
		juce::Label m_bankLabel;
		juce::ComboBox m_bank;
		juce::ToggleButton m_hideEmpty{"Hide empty"};
		juce::Label m_count;
		juce::Viewport m_viewport;
		List m_list{*this};
		juce::TextButton m_loadPch{"Load .pch..."};
		juce::TextButton m_close{"Close"};
		std::unique_ptr<juce::FileChooser> m_chooser;
		PchUpload m_pending;			// the patch Load .pch read, until it is sent or dropped
		juce::File m_pendingFile;
		StoreCard m_card{*this};
		juce::String m_message;			// what the last Load .pch came to, for a while in the footer
		juce::uint32 m_messageUntil = 0;
		uint64_t m_uploadSerial = 0;

		g1app::PresetsLink::BankNames m_names{};
		std::vector<int> m_shown;		// the positions listed, in order
		bool m_known = false;
		uint64_t m_revision = ~0ull;
		int m_loadedBank = -1, m_loadedPosition = -1;	// the last one loaded from here
		bool m_open = false;
	};
}
