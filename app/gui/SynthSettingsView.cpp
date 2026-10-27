#include "SynthSettingsView.h"

#include "G1Skin.h"
#include "Panel.h"

#include <algorithm>

namespace g1gui
{
	namespace
	{
		// The settings' look: Mike Fiction's frame (skin/settings_panel.png, with the title, headings
		// and labels) over the knobs' four sections, and the boxes of his Nord Lead 2x settings: rounded,
		// with white text. Sizes are the panel's pixels, inside the frame.
		const juce::Colour g_heading(0xff4d587e), g_label(0xfff9f9fa), g_boxText(0xffffffff);
		// His Perf / Global page's boxes (perfDropBox): the slate half see-through, solid and darker under the mouse.
		const juce::Colour g_box(0x804e587e), g_boxHover(0xff4e5a84), g_menu(0xff767c98);
		const juce::Colour g_rule(0xff525c79), g_ruleLight(0xff8e93a6);
		constexpr float g_boxRound = 12.0f / 28.0f, g_condensed = 0.85f;	// corners: 12 on a box 28 high
		constexpr int g_frameEdge = 4;			// the frame's orange border, which the rules stop at
		constexpr int g_fadeMs = 60;			// in and out: just enough to see, quicker than the Nord Lead 2x skin's Perf page
		constexpr int g_pollHz = 5, g_rereadS = 1;	// looks for the OS's answer; asks it again every second
		constexpr int g_margin = 16, g_colW = 186;	// the name and the note from the frame's sides; the columns' spacing
		// Where the columns begin, each a label's width (g_labelW) and then its box: the boxes beside the
		// frame's labels.
		constexpr int g_colsX = -20;
		constexpr int g_labelW = 74, g_controlW = 88, g_rowH = 14, g_rowStep = 22;	// boxes 1.8 times their text's caps, as on the Nord
		constexpr int g_titleY = 10, g_titleRuleY = 34, g_firstRowY = 60, g_footerY = 242;
		constexpr int g_velocityY = 183;		// Minimum's row, under the frame's Velocity Scale
		constexpr int g_tuneY = 187;			// Master tune's centre: its disk between Global sync and the rule
		constexpr float g_textSize = 11.0f;

		// Ids are value + offset, as a ComboBox wants them above 0.
		constexpr int g_channelOff = 16;
		constexpr int g_tuneRange = 100;	// cents either way, as the ticks show it

		// A box as the Perf / Global page has it, at _r.
		void fillBox(juce::Graphics& _g, const juce::Rectangle<float> _r, const juce::Colour _c)
		{
			_g.setColour(_c);
			_g.fillRoundedRectangle(_r, _r.getHeight() * g_boxRound);
		}

		// Close, at the bottom right of each page: the way out that is not the page's button at the
		// panel's far right (or Esc, which the tooltip names).
		constexpr int g_closeW = 56;
		void styleCloseButton(juce::TextButton& _b, std::function<void()> _onClick)
		{
			_b.setTooltip("Close this page (Esc)");
			_b.setColour(juce::TextButton::buttonColourId, g_heading);
			_b.setColour(juce::TextButton::textColourOffId, g_boxText);
			_b.onClick = std::move(_onClick);
		}

		juce::String cents(const int _c) { return (_c > 0 ? "+" : "") + juce::String(_c) + " cents"; }

		// Global sync's length, in quarter notes as Clavia counted it.
		juce::String syncText(const int _beats)
		{
			return juce::String(_beats) + (_beats == 1 ? " Qtr. Note" : " Qtr. Notes");
		}

		// Halved while it can be, then to size: one big step down loses the frame's fine edge.
		juce::Image scaledTo(juce::Image _img, const int _w, const int _h)
		{
			while(_img.getWidth() >= _w * 2 && _img.getHeight() >= _h * 2)
				_img = _img.rescaled(_img.getWidth() / 2, _img.getHeight() / 2, juce::Graphics::highResamplingQuality);
			return _img.getWidth() == _w && _img.getHeight() == _h ? _img : _img.rescaled(_w, _h, juce::Graphics::highResamplingQuality);
		}

		// A page closing: faded out to the panel, or, when the other page fades in over it, left as
		// it is until that one covers it and hidden then, so the panel never shows between the two.
		// Not hidden if it was opened again meanwhile.
		template<class Page> void hidePage(Page& _page, const bool _under)
		{
			if(!_under)
			{
				juce::Desktop::getInstance().getAnimator().fadeOut(&_page, g_fadeMs);	// hidden at once, a picture of it fades
				return;
			}
			juce::Timer::callAfterDelay(g_fadeMs + 40, [page = juce::Component::SafePointer<Page>(&_page)]
			{
				if(page != nullptr && !page->isOpen())
					page->setVisible(false);
			});
		}
	}

	SkinImage::SkinImage(const void* _png, const int _pngSize, const int _frameSize)
		: m_source(juce::ImageCache::getFromMemory(_png, _pngSize)), m_frameSize(_frameSize)
	{
	}

	void SkinImage::draw(juce::Graphics& _g, const juce::Rectangle<float> _dest, const int _frame)
	{
		const float px = _g.getInternalContext().getPhysicalPixelScaleFactor();
		const juce::Point<int> size(std::max(1, juce::roundToInt(_dest.getWidth() * px)), std::max(1, juce::roundToInt(_dest.getHeight() * px)));
		if(size != m_size)
		{
			m_frames.clear();
			m_size = size;
		}
		auto& img = m_frames[_frame];
		if(img.isNull())
		{
			const int cols = m_frameSize ? m_source.getWidth() / m_frameSize : 1;
			img = scaledTo(m_frameSize ? m_source.getClippedImage({_frame % cols * m_frameSize, _frame / cols * m_frameSize, m_frameSize, m_frameSize}) : m_source,
				size.x, size.y);
		}
		_g.drawImage(img, _dest);
	}

	SettingsLook::SettingsLook()
		: m_knob(G1Skin::knob_png, G1Skin::knob_pngSize, 113)
		, m_shadow(G1Skin::knob_shadow_png, G1Skin::knob_shadow_pngSize)
		, m_tuneBg(G1Skin::master_tune_bg_png, G1Skin::master_tune_bg_pngSize)
	{
		setColour(juce::ComboBox::textColourId, g_boxText);
		setColour(juce::TextEditor::textColourId, g_boxText);
		setColour(juce::TextEditor::highlightColourId, g_boxText);	// the selection white, so it shows on the boxes
		setColour(juce::TextEditor::highlightedTextColourId, g_heading);
		setColour(juce::TextEditor::focusedOutlineColourId, g_label);
		setColour(juce::CaretComponent::caretColourId, g_boxText);
		setColour(juce::PopupMenu::backgroundColourId, g_menu);
		setColour(juce::PopupMenu::textColourId, g_boxText);
		setColour(juce::PopupMenu::highlightedBackgroundColourId, g_heading);
		setColour(juce::PopupMenu::highlightedTextColourId, g_boxText);
	}

	juce::Font SettingsLook::getComboBoxFont(juce::ComboBox&) { return juce::FontOptions(g_textSize); }
	juce::Font SettingsLook::getPopupMenuFont() { return juce::FontOptions(g_textSize); }

	// A rounded box with no arrow, its text centred over all of it.
	void SettingsLook::drawComboBox(juce::Graphics& _g, const int _w, const int _h, bool, int, int, int, int, juce::ComboBox& _box)
	{
		const auto c = _box.isEnabled() ? (_box.isMouseOver(true) ? g_boxHover : g_box) : g_box.withMultipliedAlpha(0.5f);
		fillBox(_g, {0, 0, static_cast<float>(_w), static_cast<float>(_h)}, c);
	}

	void SettingsLook::positionComboBoxText(juce::ComboBox& _box, juce::Label& _label)
	{
		_label.setBounds(_box.getLocalBounds().reduced(4, 0));
		_label.setFont(getComboBoxFont(_box));
		_label.setJustificationType(juce::Justification::centred);
		_label.setMinimumHorizontalScale(0.75f);
	}

	void SettingsLook::fillTextEditorBackground(juce::Graphics& _g, const int _w, const int _h, juce::TextEditor& _editor)
	{
		fillBox(_g, {0, 0, static_cast<float>(_w), static_cast<float>(_h)}, _editor.isMouseOver(true) || _editor.hasKeyboardFocus(true) ? g_boxHover : g_box);
	}

	void SettingsLook::drawTextEditorOutline(juce::Graphics& _g, const int _w, const int _h, juce::TextEditor& _editor)
	{
		if(!_editor.hasKeyboardFocus(true))
			return;
		_g.setColour(g_label.withAlpha(0.6f));
		_g.drawRoundedRectangle(juce::Rectangle<float>(0, 0, static_cast<float>(_w), static_cast<float>(_h)).reduced(0.5f), static_cast<float>(_h) * g_boxRound, 1.0f);
	}

	// As the panel draws its knobs (Panel::paint, KnobLook), at its scale: the shadow's disk under
	// the cap, whose 128 frames turn through the same ±120° as the ticks round it.
	void SettingsLook::drawRotarySlider(juce::Graphics& _g, const int _x, const int _y, const int _w, const int _h, const float _pos, float, float, juce::Slider&)
	{
		constexpr float s = Panel::SkinScale;
		const auto c = juce::Rectangle<int>(_x, _y, _w, _h).toFloat().getCentre();
		m_shadow.draw(_g, {c.x - 33.0f * s, c.y - 33.0f * s, 100.0f * s, 101.0f * s});
		m_knob.draw(_g, {c.x - 56.0f * s, c.y - 56.0f * s, 113.0f * s, 113.0f * s}, juce::roundToInt(_pos * 127.0f));
	}

	// Mike Fiction's disk with its ticks, marks and name (skin/master_tune_bg.png, 151 x 180), at
	// the background's scale: its disk is the size of the panel's red ones, and sits on the knob's
	// centre as they do (the disk's own centre, 77, 79, half a pixel past it).
	void SettingsLook::drawTuneBackground(juce::Graphics& _g, const juce::Point<float> _centre)
	{
		constexpr float s = Panel::SkinScale;
		m_tuneBg.draw(_g, {_centre.x - 76.5f * s, _centre.y - 78.5f * s, 151.0f * s, 180.0f * s});
	}

	ValueBox::ValueBox(const int _min, const int _max) : m_min(_min), m_max(_max), m_value(_min)
	{
		setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
		m_editor.setFont(juce::FontOptions(g_textSize));
		m_editor.setJustification(juce::Justification::centred);
		m_editor.setIndents(4, 0);
		m_editor.setSelectAllWhenFocused(true);	// typing replaces the value
		m_editor.setInputRestrictions(juce::String(_max).length(), "0123456789");
		m_editor.onReturnKey = [this] { commit(); };
		m_editor.onFocusLost = [this] { commit(); };
		m_editor.onEscapeKey = [this] { cancel(); };
		addChildComponent(m_editor);
	}

	void ValueBox::setValue(const int _value)
	{
		if(_value == m_value)
			return;
		m_value = _value;	// as the OS says, even outside the range: only what is set here is kept in it
		repaint();
	}

	void ValueBox::change(const int _value)
	{
		const int v = std::clamp(_value, m_min, m_max);
		if(v == m_value)
			return;
		m_value = v;
		repaint();
		if(onChange)
			onChange();
	}

	void ValueBox::paint(juce::Graphics& _g)
	{
		const auto c = isEnabled() ? (isMouseOver(true) ? g_boxHover : g_box) : g_box.withMultipliedAlpha(0.5f);
		fillBox(_g, getLocalBounds().toFloat(), c);
		if(m_editor.isVisible())
			return;

		// Two small solid triangles at the right, up over down: it can be dragged and scrolled.
		constexpr float w = 5.0f, h = 3.0f, gap = 2.5f, right = 6.0f;
		const float x = static_cast<float>(getWidth()) - right - w, mid = static_cast<float>(getHeight()) * 0.5f;
		juce::Path arrows;
		arrows.addTriangle(x, mid - gap * 0.5f, x + w, mid - gap * 0.5f, x + w * 0.5f, mid - gap * 0.5f - h);
		arrows.addTriangle(x, mid + gap * 0.5f, x + w, mid + gap * 0.5f, x + w * 0.5f, mid + gap * 0.5f + h);
		_g.setColour(g_boxText.withAlpha(0.8f));
		_g.fillPath(arrows);

		_g.setColour(g_boxText);
		_g.setFont(juce::FontOptions(g_textSize));
		_g.drawText(juce::String(m_value), getLocalBounds(), juce::Justification::centred);
	}

	void ValueBox::resized()
	{
		m_editor.setBounds(getLocalBounds());
	}

	// Up raises it, a step every 3 pixels from where the drag began.
	void ValueBox::mouseDown(const juce::MouseEvent&)
	{
		m_dragFrom = m_value;
	}

	void ValueBox::mouseDrag(const juce::MouseEvent& _e)
	{
		if(isEnabled() && !m_editor.isVisible())
			change(m_dragFrom - _e.getDistanceFromDragStartY() / 3);
	}

	void ValueBox::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& _wheel)
	{
		if(!isEnabled() || m_editor.isVisible() || _wheel.deltaY == 0.0f)
			return;
		change(m_value + (_wheel.deltaY > 0 ? 1 : -1));
	}

	void ValueBox::mouseDoubleClick(const juce::MouseEvent&)
	{
		if(!isEnabled())
			return;
		m_editor.setText(juce::String(m_value), false);
		m_editor.setVisible(true);
		m_editor.grabKeyboardFocus();
		m_editor.selectAll();
		repaint();
	}

	void ValueBox::commit()
	{
		if(!m_editor.isVisible())
			return;
		const auto text = m_editor.getText().trim();
		m_editor.setVisible(false);
		repaint();
		if(text.isNotEmpty())
			change(text.getIntValue());
	}

	void ValueBox::cancel()
	{
		m_editor.setVisible(false);
		repaint();
	}

	SynthSettingsView::SynthSettingsView(g1app::SynthSettingsLink& _link)
		: m_link(_link), m_frame(G1Skin::settings_panel_png, G1Skin::settings_panel_pngSize)
	{
		setWantsKeyboardFocus(true);
		setLookAndFeel(&m_look);

		auto combo = [this](juce::ComboBox& _c)
		{
			_c.onChange = [this] { apply(); };
			addAndMakeVisible(_c);
		};
		auto choices = [&](juce::ComboBox& _c, const juce::StringArray& _items)
		{
			for(int i = 0; i < _items.size(); ++i)
				_c.addItem(_items[i], i + 1);
			combo(_c);
		};

		// The labels the frame does not have, as its own: small white condensed capitals.
		auto label = [this](juce::Label& _l, const juce::String& _text)
		{
			_l.setFont(juce::FontOptions(g_textSize).withHorizontalScale(g_condensed));
			_l.setText(_text, juce::dontSendNotification);
			_l.setColour(juce::Label::textColourId, g_label);
			_l.setJustificationType(juce::Justification::centredRight);
			_l.setInterceptsMouseClicks(false, false);
			addAndMakeVisible(_l);
		};
		label(m_nameLabel, "NAME");
		m_name.setFont(juce::FontOptions(g_textSize));
		m_name.setIndents(6, 0);
		m_name.setJustification(juce::Justification::centred);	// as the boxes' text
		m_name.setInputRestrictions(16);
		m_name.onReturnKey = [this] { apply(); };
		m_name.onFocusLost = [this] { apply(); };
		addAndMakeVisible(m_name);

		for(size_t i = 0; i < 4; ++i)
		{
			for(int ch = 0; ch < 16; ++ch)
				m_channels[i].addItem(juce::String(ch + 1), ch + 1);
			m_channels[i].addItem("Off", g_channelOff + 1);
			combo(m_channels[i]);
		}

		choices(m_clock, {"Internal", "External"});
		m_bpm.onChange = [this] { apply(); };
		m_bpm.setTooltip("24 to 240 BPM. Drag up or down, scroll, or double-click to type");
		addAndMakeVisible(m_bpm);
		for(int beats = 1; beats <= 32; ++beats)
			m_sync.addItem(syncText(beats), beats);
		combo(m_sync);
		m_tune.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
		m_tune.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
		m_tune.setRange(-g_tuneRange, g_tuneRange, 1);
		m_tune.setDoubleClickReturnValue(true, 0);
		m_tune.onValueChange = [this] { apply(); };
		addAndMakeVisible(m_tune);

		choices(m_knob, {"Immediate", "Hook"});
		choices(m_pedal, {"Normal", "Inverted"});
		choices(m_program, {"Off", "Receive", "Send", "Send and receive"});
		choices(m_leds, {"Active", "Off"});
		choices(m_keyboardMode, {"Active Slot", "Selected Slots"});
		m_keyboardMode.setTooltip("Selected Slots: a note on a selected slot's channel plays every selected slot, each on its own channel "
			"(and in its own octave, with Octave Shift Enabled)");
		label(m_keyboardModeLabel, "KEYB MODE");
		choices(m_midiCc, {"Active Slot", "Selected Slots"});
		m_midiCc.setTooltip("Selected Slots: a controller on a selected slot's channel goes to every selected slot, each on its own channel. "
			"Kept by G1-Emu, not by the G1");
		m_midiCc.onChange = [this]
		{
			if(setSelectedControllers)
				setSelectedControllers(m_midiCc.getSelectedId() == 2);
			lcdTips();
		};
		label(m_midiCcLabel, "MIDI CC MODE");
		choices(m_octaveShift, {"Enabled", "Disabled"});
		m_octaveShift.setTooltip("Enabled: each slot's notes are moved by the octave shift its patch carries, as on the keyboard model, "
			"and the OCT SHIFT keys change it. Disabled: the notes play as sent, as on the rack, which ignores the shift; the LEDs still show it. "
			"Kept by G1-Emu, not by the G1");
		m_octaveShift.onChange = [this]
		{
			if(setOctaveShift)
				setOctaveShift(m_octaveShift.getSelectedId() == 1);
			lcdTips();
		};
		label(m_octaveShiftLabel, "OCT SHIFT");
		for(auto* v : {&m_velMin, &m_velMax})
		{
			v->onChange = [this] { apply(); };
			v->setTooltip("0 to 127. Drag up or down, scroll, or double-click to type");
			addAndMakeVisible(*v);
		}

		m_note.setText("Changes reach the G1 at once. To keep them after a restart,\n"
			"press Shift + Store on the panel (Save Synth Settings).", juce::dontSendNotification);
		m_note.setFont(juce::FontOptions(g_textSize));
		m_note.setColour(juce::Label::textColourId, g_heading);
		m_note.setJustificationType(juce::Justification::centredLeft);
		addAndMakeVisible(m_note);

		for(auto* c : getChildren())
			c->setEnabled(false);	// until the OS has said what it has
		styleCloseButton(m_close, [this] { close(); });
		addAndMakeVisible(m_close);	// after the above: it closes the page even before the OS answers
		addMouseListener(this, true);	// the boxes' hover colour (mouseEnter)
	}

	SynthSettingsView::~SynthSettingsView()
	{
		setLookAndFeel(nullptr);
	}

	void SynthSettingsView::octaveShiftChanged()
	{
		m_octaveShift.setSelectedId(octaveShift && octaveShift() ? 1 : 2, juce::dontSendNotification);
		lcdTips();
	}

	void SynthSettingsView::open(const juce::Rectangle<int> _frame)
	{
		m_link.read();
		m_midiCc.setSelectedId(selectedControllers && selectedControllers() ? 2 : 1, juce::dontSendNotification);
		m_octaveShift.setSelectedId(octaveShift && octaveShift() ? 1 : 2, juce::dontSendNotification);
		setBounds(_frame);
		m_open = true;
		juce::Desktop::getInstance().getAnimator().fadeIn(this, g_fadeMs);
		toFront(true);
		m_ticks = 0;
		startTimerHz(g_pollHz);
		timerCallback();
	}

	void SynthSettingsView::close(const bool _under)
	{
		apply();	// a name still being typed
		stopTimer();
		m_open = false;
		hidePage(*this, _under);
		if(onClose)
			onClose();
	}

	void SynthSettingsView::paint(juce::Graphics& _g)
	{
		const auto c = getLocalBounds();
		m_frame.draw(_g, c.toFloat());
		m_look.drawTuneBackground(_g, m_tune.getBounds().toFloat().getCentre());

		// The rules as the Nord's: a dark line with a light one under it, from border to border.
		for(const int y : {g_titleRuleY, g_footerY})
		{
			const auto rule = juce::Rectangle<int>(c.getX() + g_frameEdge, c.getY() + y - 1, c.getWidth() - 2 * g_frameEdge, 3);
			_g.setColour(g_rule);
			_g.fillRect(rule);
			_g.setColour(g_ruleLight);
			_g.fillRect(rule.withY(rule.getBottom()).withHeight(1));
		}

		// The title, headings and labels are the frame's (Mike Fiction's art).
		if(!m_haveSettings)
		{
			_g.setColour(g_heading);
			_g.setFont(juce::FontOptions(g_textSize));
			_g.drawText("Asking the G1...", c.getX() + 150, c.getY() + g_titleY, 150, g_rowH, juce::Justification::centredLeft);
		}
	}

	void SynthSettingsView::resized()
	{
		const auto c = getLocalBounds();
		// Each box right of its label in the frame's art.
		auto row = [&](const int _col, const int _y, juce::Component& _control)
		{
			_control.setBounds(c.getX() + g_colsX + _col * g_colW + g_labelW + 4, c.getY() + _y, g_controlW, g_rowH);
		};
		auto rowY = [](const int _i) { return g_firstRowY + _i * g_rowStep; };
		m_name.setBounds(c.getRight() - g_margin - 130, c.getY() + g_titleY, 130, g_rowH);
		m_nameLabel.setBounds(m_name.getX() - 4 - 40, c.getY() + g_titleY, 40, g_rowH);

		for(size_t i = 0; i < 4; ++i)
			row(0, rowY(static_cast<int>(i)), m_channels[i]);

		row(1, rowY(0), m_clock);
		row(1, rowY(1), m_bpm);
		row(1, rowY(2), m_sync);
		// Level with Minimum and Maximum, with labels of their own: the frame's Control section has
		// no room for them yet.
		auto labelled = [&](const int _y, juce::Component& _control, juce::Label& _label)
		{
			row(1, _y, _control);
			_label.setBounds(_control.getX() - 4 - g_labelW, _control.getY(), g_labelW, g_rowH);
		};
		labelled(g_velocityY - g_rowStep, m_octaveShift, m_octaveShiftLabel);
		labelled(g_velocityY, m_midiCc, m_midiCcLabel);
		labelled(g_velocityY + g_rowStep, m_keyboardMode, m_keyboardModeLabel);
		// The knob under the MIDI channels' boxes, centred on them, in its disk (paint); its value
		// is on the panel's display while the mouse is on it (lcdTips).
		const int tuneX = c.getX() + g_colsX + g_labelW + 4 + g_controlW / 2;
		m_tune.setBounds(tuneX - 28, c.getY() + g_tuneY - 28, 56, 56);

		row(2, rowY(0), m_knob);
		row(2, rowY(1), m_pedal);
		row(2, rowY(2), m_program);
		row(2, rowY(3), m_leds);
		row(2, g_velocityY, m_velMin);
		row(2, g_velocityY + g_rowStep, m_velMax);

		m_close.setBounds(c.getRight() - g_margin - g_closeW, c.getY() + g_footerY + 8, g_closeW, g_rowH + 6);
		m_note.setBounds(c.getX() + g_margin - 4, c.getY() + g_footerY + 4, m_close.getX() - 8 - (c.getX() + g_margin - 4), 32);
	}

	// The mouse going in or out of a box's text (its child) is not the box's own event, so the box
	// would keep or miss its hover colour: the control it is part of is redrawn here.
	void SynthSettingsView::mouseEnter(const juce::MouseEvent& _e) { repaintControl(_e); }
	void SynthSettingsView::mouseExit(const juce::MouseEvent& _e) { repaintControl(_e); }

	void SynthSettingsView::repaintControl(const juce::MouseEvent& _e)
	{
		for(auto* c = _e.eventComponent; c != nullptr && c != this; c = c->getParentComponent())
			if(c->getParentComponent() == this)
				c->repaint();
	}

	bool SynthSettingsView::keyPressed(const juce::KeyPress& _key)
	{
		if(_key == juce::KeyPress::escapeKey)
		{
			close();
			return true;
		}
		return false;
	}

	void SynthSettingsView::timerCallback()
	{
		// Asked again now and then while it is open: an editor, or the panel's System menu, may have
		// changed them meanwhile. The link waits while an editor is talking.
		if(++m_ticks % (g_pollHz * g_rereadS) == 0)
			m_link.read();
		g1app::SynthSettings s;
		uint64_t revision = 0;
		if(!m_link.settings(s, revision) || revision == m_revision)
			return;
		m_revision = revision;
		m_known = s;
		if(!m_haveSettings)
		{
			m_haveSettings = true;
			for(auto* c : getChildren())
				c->setEnabled(true);
			repaint();
		}
		show(s);
	}

	void SynthSettingsView::show(const g1app::SynthSettings& _s)
	{
		const auto n = juce::dontSendNotification;
		// A value the lists leave out (the OS's own range is wider) is added, so it shows as it is.
		auto select = [n](juce::ComboBox& _c, const int _id, const juce::String& _text)
		{
			if(_c.indexOfItemId(_id) < 0)
				_c.addItem(_text, _id);
			_c.setSelectedId(_id, n);
		};
		if(!m_name.hasKeyboardFocus(false))
			m_name.setText(_s.name, false);
		for(size_t i = 0; i < 4; ++i)
			select(m_channels[i], std::min(_s.midiChannel[i], g_channelOff) + 1, juce::String(_s.midiChannel[i] + 1));
		m_clock.setSelectedId(_s.clockInternal ? 1 : 2, n);
		m_bpm.setValue(_s.clockBpm);
		m_sync.setSelectedId(_s.globalSync + 1, n);
		m_tune.setValue(std::clamp(_s.masterTune, -g_tuneRange, g_tuneRange), n);
		m_knob.setSelectedId(_s.knobMode + 1, n);
		m_pedal.setSelectedId(_s.pedalPolarity + 1, n);
		m_program.setSelectedId(1 + (_s.programChangeReceive ? 1 : 0) + (_s.programChangeSend ? 2 : 0), n);
		m_leds.setSelectedId(_s.ledsActive ? 1 : 2, n);
		m_keyboardMode.setSelectedId(_s.keyboardMode + 1, n);
		m_velMin.setValue(_s.velScaleMin);
		m_velMax.setValue(_s.velScaleMax);
		lcdTips();
	}

	// Each control's name and value on the panel's display, while the mouse is on it (or turns it),
	// as the synth's own controls have theirs.
	void SynthSettingsView::lcdTips()
	{
		auto tip = [](juce::Component& _c, const juce::String& _name, const juce::String& _value) { setLcdTip(_c, _name + "\t" + _value); };
		tip(m_name, "Synth Name", m_name.getText());
		for(size_t i = 0; i < 4; ++i)
			tip(m_channels[i], juce::String("Slot ") + static_cast<char>('A' + i) + " MIDI Ch", m_channels[i].getText());
		tip(m_clock, "MIDI Clock", m_clock.getText());
		tip(m_bpm, "Clock Rate", juce::String(m_bpm.getValue()) + " BPM");
		tip(m_sync, "Global Sync", m_sync.getText());
		tip(m_tune, "Master Tune", cents(juce::roundToInt(m_tune.getValue())));
		tip(m_knob, "Knob Mode", m_knob.getText());
		tip(m_pedal, "Pedal Polarity", m_pedal.getText());
		tip(m_program, "Program Change", m_program.getSelectedId() == 4 ? juce::String("Snd/Rcv") : m_program.getText());	// fits the display
		tip(m_leds, "Editor LEDs", m_leds.getText());
		// Selected Slots shortened, so the name fits beside it on the display.
		const auto slots = [](const juce::ComboBox& _c) { return _c.getSelectedId() == 2 ? juce::String("Sel.Slots") : _c.getText(); };
		tip(m_keyboardMode, "Keyboard Mode", slots(m_keyboardMode));
		tip(m_midiCc, "MIDI CC Mode", slots(m_midiCc));
		tip(m_octaveShift, "Octave Shift", m_octaveShift.getText());
		tip(m_velMin, "Velocity Min", juce::String(m_velMin.getValue()));
		tip(m_velMax, "Velocity Max", juce::String(m_velMax.getValue()));
	}

	void SynthSettingsView::apply()
	{
		lcdTips();	// what was changed, on the display at once
		if(!m_haveSettings)
			return;
		auto s = m_known;
		s.name = m_name.getText().substring(0, 16).toStdString();
		for(size_t i = 0; i < 4; ++i)
			s.midiChannel[i] = m_channels[i].getSelectedId() - 1;
		s.clockInternal = m_clock.getSelectedId() == 1;
		s.clockBpm = m_bpm.getValue();
		s.globalSync = m_sync.getSelectedId() - 1;
		const int tune = juce::roundToInt(m_tune.getValue());
		if(tune != std::clamp(m_known.masterTune, -g_tuneRange, g_tuneRange))
			s.masterTune = tune;	// one beyond the knob's range stays until it is turned
		s.knobMode = m_knob.getSelectedId() - 1;
		s.pedalPolarity = m_pedal.getSelectedId() - 1;
		const int program = m_program.getSelectedId() - 1;
		s.programChangeReceive = (program & 1) != 0;
		s.programChangeSend = (program & 2) != 0;
		s.ledsActive = m_leds.getSelectedId() == 1;
		s.keyboardMode = m_keyboardMode.getSelectedId() - 1;
		s.velScaleMin = m_velMin.getValue();
		s.velScaleMax = m_velMax.getValue();
		if(s == m_known)
			return;		// nothing changed (the name editor losing the focus)
		m_known = s;
		m_link.write(s);
	}

	// ____________________________________________________________________________________________
	// Presets

	namespace
	{
		const juce::Colour g_loaded(0xffe89d32);	// the frame's orange: the patch loaded from here
		constexpr int g_listTop = g_titleRuleY + 6, g_listRowH = 18, g_listCols = 3;
		constexpr int g_numberW = 22;				// "07" before each name
	}

	// The footer's top: the G1's, 41 above the frame's bottom (242 in its 283), and as much higher as
	// a lower frame needs (the Micro's box, 2026-10-27), so its row is never cut off.
	int PresetsView::footerY() const
	{
		return std::min(g_footerY, getHeight() - 41);
	}

	void PresetsView::setBankCount(const int _count)
	{
		m_bank.clear(juce::dontSendNotification);
		for(int b = 0; b < std::clamp(_count, 1, static_cast<int>(g1app::PresetsLink::Banks)); ++b)
			m_bank.addItem("Bank " + juce::String(b + 1), b + 1);
		m_bank.setSelectedId(1, juce::dontSendNotification);
		m_bank.setEnabled(_count > 1);
	}

	PresetsView::PresetsView(g1app::PresetsLink& _link, std::function<int()> _activeSlot)
		: m_link(_link), m_activeSlot(std::move(_activeSlot)), m_frame(G1Skin::presets_panel_png, G1Skin::presets_panel_pngSize)
	{
		setWantsKeyboardFocus(true);
		setLookAndFeel(&m_look);

		// Bank's label as the Settings page's Name: small white condensed capitals.
		m_bankLabel.setFont(juce::FontOptions(g_textSize).withHorizontalScale(g_condensed));
		m_bankLabel.setText("BANK", juce::dontSendNotification);
		m_bankLabel.setColour(juce::Label::textColourId, g_label);
		m_bankLabel.setJustificationType(juce::Justification::centredRight);
		addAndMakeVisible(m_bankLabel);
		for(int b = 0; b < g1app::PresetsLink::Banks; ++b)
			m_bank.addItem("Bank " + juce::String(b + 1), b + 1);
		m_bank.setSelectedId(1, juce::dontSendNotification);
		m_bank.onChange = [this]
		{
			m_link.readBank(m_bank.getSelectedId() - 1);
			m_revision = ~0ull;
			showBank();
			m_viewport.setViewPosition(0, 0);
		};
		addAndMakeVisible(m_bank);

		m_hideEmpty.setToggleState(true, juce::dontSendNotification);
		m_hideEmpty.setColour(juce::ToggleButton::textColourId, g_label);
		m_hideEmpty.setColour(juce::ToggleButton::tickColourId, g_label);
		m_hideEmpty.setColour(juce::ToggleButton::tickDisabledColourId, g_label);
		m_hideEmpty.setTooltip("List only the positions that hold a patch");
		m_hideEmpty.onClick = [this]
		{
			if(onHideEmptyChanged)
				onHideEmptyChanged(m_hideEmpty.getToggleState());
			showBank();
		};
		addAndMakeVisible(m_hideEmpty);

		m_count.setFont(juce::FontOptions(g_textSize));
		m_count.setColour(juce::Label::textColourId, g_label);
		m_count.setJustificationType(juce::Justification::centredLeft);
		addAndMakeVisible(m_count);

		m_loadPch.setTooltip("Send a .pch file to the synth, and store it in a bank if you like");
		m_loadPch.setColour(juce::TextButton::buttonColourId, g_heading);
		m_loadPch.setColour(juce::TextButton::textColourOffId, g_boxText);
		m_loadPch.onClick = [this] { choosePch(); };
		addAndMakeVisible(m_loadPch);
		styleCloseButton(m_close, [this] { close(); });
		addAndMakeVisible(m_close);
		addChildComponent(m_card);

		m_viewport.setViewedComponent(&m_list, false);
		m_viewport.setScrollBarsShown(true, false);
		m_viewport.setScrollBarThickness(8);
		m_viewport.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId, g_heading);
		addAndMakeVisible(m_viewport);
	}

	PresetsView::~PresetsView()
	{
		setLookAndFeel(nullptr);
	}

	void PresetsView::open(const juce::Rectangle<int> _frame)
	{
		setBounds(_frame);
		m_link.readBank(m_bank.getSelectedId() - 1);	// it may have changed since (an editor stored one)
		m_revision = ~0ull;
		showBank();
		m_open = true;
		juce::Desktop::getInstance().getAnimator().fadeIn(this, g_fadeMs);
		toFront(true);
		startTimerHz(g_pollHz);
	}

	void PresetsView::close(const bool _under)
	{
		if(!m_open)		// a fade out started on a closed page would hide it once opened again
			return;
		stopTimer();
		m_open = false;
		hidePage(*this, _under);
		if(onClose)
			onClose();
	}

	void PresetsView::timerCallback()
	{
		const auto status = m_link.uploadStatus();
		if(status.serial != m_uploadSerial)
		{
			m_uploadSerial = status.serial;
			m_message = status.message;
			m_messageUntil = juce::Time::getMillisecondCounter() + 8000;
		}
		if(m_link.revision() != m_revision)
		{
			showBank();
			if(m_card.isVisible())
				m_card.refresh();
		}
		else
			repaint(getLocalBounds().withTop(footerY()));	// "Reading..." comes and goes
	}

	// The file, read and made into packets at once: what cannot be read is said here, before
	// asking where to put it.
	void PresetsView::choosePch()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Load a patch into the synth", juce::File(), "*.pch");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this](const juce::FileChooser& _c)
		{
			if(_c.getResult() != juce::File())
				loadPch(_c.getResult());
		});
	}

	void PresetsView::loadPch(const juce::File& _file)
	{
		m_pendingFile = _file;
		m_pending = preparePch(_file, m_activeSlot ? std::clamp(m_activeSlot(), 0, 3) : 0);
		if(!m_pending.ok())
		{
			m_message = "Cannot load it: " + m_pending.error + ".";
			m_messageUntil = juce::Time::getMillisecondCounter() + 8000;
			repaint();
			return;
		}
		m_card.open(m_pending.name, m_bank.getSelectedId() - 1);
	}

	void PresetsView::send(const int _bank, const int _position)
	{
		// The packets name the slot they go to (and their checksums count it): made again for the
		// slot lit now, which may not be the one lit when the file was picked.
		const int slot = m_activeSlot ? std::clamp(m_activeSlot(), 0, 3) : 0;
		auto up = preparePch(m_pendingFile, slot);
		if(!up.ok())
			return;
		m_link.upload(slot, std::move(up.frames), std::move(up.abort), _bank, _position);
		m_pending = {};
		if(_bank >= 0)
		{
			if(m_bank.getSelectedId() != _bank + 1)
				m_bank.setSelectedId(_bank + 1, juce::dontSendNotification);
			m_loadedBank = _bank;
			m_loadedPosition = _position;
			m_revision = ~0ull;
			showBank();
		}
	}

	void PresetsView::showBank()
	{
		m_revision = m_link.revision();
		const int bank = m_bank.getSelectedId() - 1;
		m_known = m_link.bank(bank, m_names);
		m_shown.clear();
		int used = 0;
		for(int p = 0; p < g1app::PresetsLink::Positions; ++p)
		{
			const bool empty = m_names[static_cast<size_t>(p)].empty();
			used += empty ? 0 : 1;
			if(m_known && (!empty || !m_hideEmpty.getToggleState()))
				m_shown.push_back(p);
		}
		m_count.setText(m_known ? juce::String(used) + " / 99" : juce::String(), juce::dontSendNotification);
		const int width = m_viewport.getMaximumVisibleWidth();
		m_list.setSize(width, std::max(m_viewport.getMaximumVisibleHeight(), m_list.rowsFor(static_cast<int>(m_shown.size())) * g_listRowH));
		m_list.repaint();
		repaint();
	}

	void PresetsView::refresh()
	{
		m_link.readBank(m_bank.getSelectedId() - 1);
	}

	void PresetsView::setHideEmpty(const bool _on)
	{
		m_hideEmpty.setToggleState(_on, juce::dontSendNotification);
		showBank();
	}

	void PresetsView::loadAt(const int _position)
	{
		const int bank = m_bank.getSelectedId() - 1;
		const int slot = m_activeSlot ? m_activeSlot() : 0;
		m_link.load(slot, bank, _position);
		m_loadedBank = bank;
		m_loadedPosition = _position;
		m_list.repaint();
	}

	juce::String PresetsView::footer() const
	{
		if(m_message.isNotEmpty() && (m_link.uploadStatus().busy || juce::Time::getMillisecondCounter() < m_messageUntil))
			return m_message;
		const int bank = m_bank.getSelectedId() - 1;
		if(m_link.reading(bank))
			return "Reading bank " + juce::String(bank + 1) + " from the synth...";
		if(!m_known)
			return "The synth has not answered yet.";
		if(m_shown.empty())
			return "Bank " + juce::String(bank + 1) + " is empty.";
		const char slots[] = {'A', 'B', 'C', 'D'};
		const int slot = m_activeSlot ? std::clamp(m_activeSlot(), 0, 3) : 0;
		return juce::String("Click a patch to load it into slot ") + slots[slot] + ".";
	}

	// Mike Fiction's Presets frame (its title lettered in), the rules as the Synth Settings page
	// has them, the list, and the note below.
	void PresetsView::paint(juce::Graphics& _g)
	{
		const auto c = getLocalBounds();
		m_frame.draw(_g, c.toFloat());

		for(const int y : {g_titleRuleY, footerY()})
		{
			const auto rule = juce::Rectangle<int>(c.getX() + g_frameEdge, c.getY() + y - 1, c.getWidth() - 2 * g_frameEdge, 3);
			_g.setColour(g_rule);
			_g.fillRect(rule);
			_g.setColour(g_ruleLight);
			_g.fillRect(rule.withY(rule.getBottom()).withHeight(1));
		}

		_g.setColour(g_label);
		_g.setFont(juce::FontOptions(g_textSize));
		_g.drawText(footer(), c.getX() + g_margin, c.getY() + footerY() + 4, m_loadPch.getX() - 8 - g_margin, c.getHeight() - footerY() - 8,
			juce::Justification::centredLeft, true);
	}

	// The title bar as the Settings page's: Bank and its box at the right, where Name is, and
	// the filter and the count together in the middle.
	void PresetsView::resized()
	{
		const auto c = getLocalBounds();
		m_bank.setBounds(c.getRight() - g_margin - 90, c.getY() + g_titleY, 90, g_rowH);
		m_bankLabel.setBounds(m_bank.getX() - 4 - 40, c.getY() + g_titleY, 40, g_rowH);
		constexpr int hideW = 80, countW = 52, gap = 4;
		const int left = c.getCentreX() - (hideW + gap + countW) / 2;
		m_hideEmpty.setBounds(left, c.getY() + g_titleY - 1, hideW, g_rowH + 2);	// JUCE sizes its text to its height
		m_count.setBounds(left + hideW + gap, c.getY() + g_titleY, countW, g_rowH);
		m_viewport.setBounds(c.getX() + g_margin - 6, c.getY() + g_listTop, c.getWidth() - 2 * g_margin + 12, footerY() - g_listTop - 6);
		m_close.setBounds(c.getRight() - g_margin - g_closeW, c.getY() + footerY() + 8, g_closeW, g_rowH + 6);
		m_loadPch.setBounds(m_close.getX() - 8 - 84, c.getY() + footerY() + 8, 84, g_rowH + 6);
		m_card.setBounds(m_viewport.getBounds().reduced(60, 18));
		showBank();
	}

	bool PresetsView::keyPressed(const juce::KeyPress& _key)
	{
		if(_key != juce::KeyPress::escapeKey)
			return false;
		close();
		return true;
	}

	// ____________________________________________________________________________________________
	// Presets: Load .pch's question

	PresetsView::StoreCard::StoreCard(PresetsView& _owner) : m_owner(_owner)
	{
		for(auto* l : {&m_bankLabel, &m_posLabel})
		{
			l->setFont(juce::FontOptions(g_textSize).withHorizontalScale(g_condensed));
			l->setColour(juce::Label::textColourId, g_label);
			l->setJustificationType(juce::Justification::centredRight);
			addAndMakeVisible(*l);
		}
		m_bankLabel.setText("BANK", juce::dontSendNotification);
		m_posLabel.setText("POSITION", juce::dontSendNotification);
		m_warning.setFont(juce::FontOptions(g_textSize));
		m_warning.setColour(juce::Label::textColourId, juce::Colour(0xffffd0a0));
		m_warning.setJustificationType(juce::Justification::centred);
		addAndMakeVisible(m_warning);

		for(int b = 0; b < g1app::PresetsLink::Banks; ++b)
			m_bank.addItem("Bank " + juce::String(b + 1), b + 1);
		m_bank.onChange = [this]
		{
			m_owner.m_link.readBank(m_bank.getSelectedId() - 1);
			m_known = false;
			fillPositions(true);
		};
		m_position.onChange = [this] { warn(); };
		addAndMakeVisible(m_bank);
		addAndMakeVisible(m_position);

		for(auto* b : {&m_store, &m_loadOnly, &m_cancel})
		{
			b->setColour(juce::TextButton::buttonColourId, g_heading);
			b->setColour(juce::TextButton::textColourOffId, g_boxText);
			addAndMakeVisible(*b);
		}
		m_store.setTooltip("Send it to the slot and store it at this bank and position");
		m_loadOnly.setTooltip("Send it to the slot only; store it nowhere");
		m_store.onClick = [this]
		{
			setVisible(false);
			m_owner.send(m_bank.getSelectedId() - 1, m_position.getSelectedId() - 1);
		};
		m_loadOnly.onClick = [this]
		{
			setVisible(false);
			m_owner.send(-1, 0);
		};
		m_cancel.onClick = [this]
		{
			setVisible(false);
			m_owner.m_pending = {};
		};
	}

	void PresetsView::StoreCard::open(const juce::String& _name, const int _bank)
	{
		m_name = _name;
		m_bank.setSelectedId(_bank + 1, juce::dontSendNotification);
		m_known = false;
		fillPositions(true);
		setVisible(true);
		toFront(true);
		repaint();
	}

	void PresetsView::StoreCard::refresh()
	{
		if(!m_known)
			fillPositions(true);
	}

	// Each position with what it holds; the first empty one picked once the bank is known.
	void PresetsView::StoreCard::fillPositions(const bool _pickEmpty)
	{
		g1app::PresetsLink::BankNames names;
		const int bank = m_bank.getSelectedId() - 1;
		const bool known = m_owner.m_link.bank(bank, names);
		const int was = m_position.getSelectedId();
		m_position.clear(juce::dontSendNotification);
		int firstEmpty = -1;
		for(int p = 0; p < g1app::PresetsLink::Positions; ++p)
		{
			const auto& n = names[static_cast<size_t>(p)];
			if(n.empty() && firstEmpty < 0)
				firstEmpty = p;
			m_position.addItem(juce::String(p + 1).paddedLeft('0', 2) + "   " + (known ? (n.empty() ? juce::String("(empty)") : juce::String(n)) : juce::String("...")), p + 1);
		}
		m_known = known;
		m_position.setSelectedId(_pickEmpty && known ? std::max(firstEmpty, 0) + 1 : std::max(was, 1), juce::dontSendNotification);
		warn();
	}

	void PresetsView::StoreCard::warn()
	{
		g1app::PresetsLink::BankNames names;
		const int bank = m_bank.getSelectedId() - 1, pos = m_position.getSelectedId() - 1;
		juce::String text;
		if(!m_owner.m_link.bank(bank, names))
			text = "Reading bank " + juce::String(bank + 1) + "...";
		else if(pos >= 0 && !names[static_cast<size_t>(pos)].empty())
			text = "Position " + juce::String(pos + 1).paddedLeft('0', 2) + " holds \"" + juce::String(names[static_cast<size_t>(pos)]) + "\": it will be replaced.";
		m_warning.setText(text, juce::dontSendNotification);
		m_store.setButtonText(text.contains("replaced") ? "Replace" : "Store");
		m_store.setEnabled(m_known);
	}

	void PresetsView::StoreCard::paint(juce::Graphics& _g)
	{
		// A card, not a box: opaque over the list, a frame of the rules' blue.
		const auto r = getLocalBounds().toFloat().reduced(1.0f);
		_g.setColour(g_menu);
		_g.fillRoundedRectangle(r, 8.0f);
		_g.setColour(g_rule);
		_g.drawRoundedRectangle(r, 8.0f, 2.0f);
		_g.setColour(g_boxText);
		_g.setFont(juce::FontOptions(g_textSize + 2.0f, juce::Font::bold));
		_g.drawText("Load \"" + m_name + "\"", getLocalBounds().removeFromTop(30).reduced(12, 0), juce::Justification::centredLeft, true);
	}

	void PresetsView::StoreCard::resized()
	{
		auto r = getLocalBounds().reduced(12, 0);
		r.removeFromTop(32);
		auto row = r.removeFromTop(g_rowH + 4);
		m_bankLabel.setBounds(row.removeFromLeft(40));
		row.removeFromLeft(4);
		m_bank.setBounds(row.removeFromLeft(80));
		row.removeFromLeft(14);
		m_posLabel.setBounds(row.removeFromLeft(60));
		row.removeFromLeft(4);
		m_position.setBounds(row.removeFromLeft(std::max(100, row.getWidth())));
		r.removeFromTop(8);
		m_warning.setBounds(r.removeFromTop(g_rowH + 4));
		auto buttons = getLocalBounds().reduced(12, 10).removeFromBottom(g_rowH + 8);
		m_cancel.setBounds(buttons.removeFromRight(70));
		buttons.removeFromRight(8);
		m_loadOnly.setBounds(buttons.removeFromRight(80));
		buttons.removeFromRight(8);
		m_store.setBounds(buttons.removeFromRight(80));
	}

	// ____________________________________________________________________________________________
	// Presets: the list

	int PresetsView::List::rowsFor(const int _count) const
	{
		return (_count + g_listCols - 1) / g_listCols;
	}

	int PresetsView::List::positionAt(const juce::Point<int> _p) const
	{
		const auto& shown = m_owner.m_shown;
		const int rows = std::max(1, rowsFor(static_cast<int>(shown.size())));
		const int colW = getWidth() / g_listCols;
		const int col = _p.x / std::max(1, colW), row = _p.y / g_listRowH;
		const int i = col * rows + row;
		if(_p.x < 0 || col >= g_listCols || row >= rows || i < 0 || i >= static_cast<int>(shown.size()))
			return -1;
		return shown[static_cast<size_t>(i)];
	}

	void PresetsView::List::paint(juce::Graphics& _g)
	{
		const auto& shown = m_owner.m_shown;
		const int rows = rowsFor(static_cast<int>(shown.size()));
		const int colW = getWidth() / g_listCols;
		const bool thisBank = m_owner.m_loadedBank == m_owner.m_bank.getSelectedId() - 1;
		for(size_t i = 0; i < shown.size(); ++i)
		{
			const int pos = shown[i];
			const int col = static_cast<int>(i) / std::max(1, rows), row = static_cast<int>(i) % std::max(1, rows);
			const auto r = juce::Rectangle<int>(col * colW, row * g_listRowH, colW - 8, g_listRowH).reduced(0, 1);
			const auto& name = m_owner.m_names[static_cast<size_t>(pos)];
			const bool loaded = thisBank && pos == m_owner.m_loadedPosition;
			if(loaded || pos == m_hover)
				fillBox(_g, r.toFloat(), loaded ? g_loaded : g_boxHover);
			_g.setFont(juce::FontOptions(g_textSize, juce::Font::bold));
			_g.setColour(loaded || pos == m_hover ? g_boxText : g_heading);
			_g.drawText(juce::String(pos + 1).paddedLeft('0', 2), r.withWidth(g_numberW).withTrimmedLeft(4), juce::Justification::centredLeft, false);
			_g.setFont(juce::FontOptions(g_textSize + 1.0f));
			_g.setColour(name.empty() ? g_label.withAlpha(0.45f) : g_label);
			_g.drawText(name.empty() ? juce::String("--") : juce::String(name), r.withTrimmedLeft(g_numberW + 6), juce::Justification::centredLeft, true);
		}
	}

	void PresetsView::List::mouseMove(const juce::MouseEvent& _e)
	{
		const int p = positionAt(_e.getPosition());
		if(p == m_hover)
			return;
		m_hover = p;
		repaint();
	}

	void PresetsView::List::mouseExit(const juce::MouseEvent&)
	{
		m_hover = -1;
		repaint();
	}

	// A click on a name loads it; on an empty position, nothing (there is nothing to load).
	void PresetsView::List::mouseUp(const juce::MouseEvent& _e)
	{
		if(_e.mouseWasDraggedSinceMouseDown())
			return;
		const int p = positionAt(_e.getPosition());
		if(p >= 0 && !m_owner.m_names[static_cast<size_t>(p)].empty())
			m_owner.loadAt(p);
	}
}
