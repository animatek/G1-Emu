#include "MicroPanel.h"

#include "g1Lib/g1mc.h"

#include <algorithm>
#include <cmath>

namespace g1gui
{
	namespace
	{
		const juce::Colour g_body(0xffd8302c), g_plum(0xff4a1a3c), g_ink(0xfff0e6df);
		const juce::Colour g_lit(0xffff3b2e), g_unlit(0xff4a1210), g_glass(0xff1c0707);
		constexpr float g_sweep = 0.75f * juce::MathConstants<float>::pi;	// the knob turns +-135 degrees

		// Where things are on the box's top, as fractions of the panel (x, y, width, height).
		juce::Rectangle<float> at(const juce::Rectangle<float> _panel, const float _x, const float _y, const float _w, const float _h)
		{
			return {_panel.getX() + _x * _panel.getWidth(), _panel.getY() + _y * _panel.getHeight(),
				_w * _panel.getWidth(), _h * _panel.getHeight()};
		}

		// A label in a plum-framed box, as the box prints them: white ink on plum (or plum on red).
		void labelBox(juce::Graphics& _g, const juce::Rectangle<float> _r, const juce::String& _text, const bool _filled, const float _font)
		{
			if(_filled)
			{
				_g.setColour(g_plum);
				_g.fillRect(_r);
			}
			_g.setColour(g_plum);
			_g.drawRect(_r, 1.5f);
			_g.setColour(_filled ? g_ink : g_plum);
			_g.setFont(juce::FontOptions(_font, juce::Font::bold));
			_g.drawFittedText(_text, _r.toNearestInt(), juce::Justification::centred, 1);
		}

		// Knobs 1-3, then the display's and the right buttons' column: the number over what Shift
		// makes of it (the box's two rows of labels).
		struct Column { float x, w; const char* top; const char* bottom; };
		constexpr std::array<Column, 5> g_columns{{
			{0.285f, 0.140f, "1", "Master Tune"}, {0.425f, 0.140f, "2", "MIDI Channel"},
			{0.565f, 0.140f, "3", "Patch Select"}, {0.705f, 0.120f, "", "Patch"}, {0.825f, 0.105f, "Note Trig", "4"}}};
	}

	void MicroButton::update()
	{
		const bool held = m_mouse || m_key;
		if(held == m_held)
			return;
		m_held = held;
		repaint();
		if(onHeld)
			onHeld(held);
	}

	void MicroButton::paint(juce::Graphics& _g)
	{
		// Held, it sinks a little and its face lightens.
		const auto r = getLocalBounds().toFloat().reduced(m_held ? 4.0f : 2.0f);
		_g.setColour(juce::Colour(m_held ? 0xff2a2a2a : 0xff121212));
		_g.fillRoundedRectangle(r, 6.0f);
		_g.setColour(juce::Colour(m_held ? 0xff6a6a6a : 0xff3c3c3c));
		_g.drawRoundedRectangle(r.reduced(3.0f), 5.0f, 1.5f);
	}

	MicroKnob::MicroKnob(const juce::String& _label, const int _default) : m_label(_label), m_value(_default), m_default(_default)
	{
		setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
	}

	void MicroKnob::setValue(const int _v, const bool _notify)
	{
		const auto v = std::clamp(_v, 0, 255);
		if(v == m_value)
			return;
		m_value = v;
		repaint();
		if(_notify && onChange)
			onChange(m_value);
	}

	void MicroKnob::mouseDown(const juce::MouseEvent&) { m_dragStart = m_value; }

	void MicroKnob::mouseDrag(const juce::MouseEvent& _e)
	{
		// 200 pixels for the whole range; Shift for fine.
		const float perPixel = _e.mods.isShiftDown() ? 0.2f : 1.275f;
		setValue(m_dragStart + static_cast<int>(std::lround(-static_cast<float>(_e.getDistanceFromDragStartY()) * perPixel)), true);
	}

	void MicroKnob::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& _w)
	{
		setValue(m_value + static_cast<int>(std::lround(_w.deltaY * 40.0f)), true);
	}

	void MicroKnob::paint(juce::Graphics& _g)
	{
		auto area = getLocalBounds().toFloat();
		auto labelArea = m_label.isEmpty() ? juce::Rectangle<float>() : area.removeFromBottom(22.0f);
		const auto d = std::min(area.getWidth(), area.getHeight()) - 8.0f;
		const auto knob = juce::Rectangle<float>(d, d).withCentre(area.getCentre());
		_g.setColour(juce::Colour(0xff1d1d1d));
		_g.fillEllipse(knob);
		_g.setColour(juce::Colour(0xff3a3a3a));
		_g.drawEllipse(knob.reduced(1.0f), 2.0f);
		const auto angle = (static_cast<float>(m_value) / 255.0f * 2.0f - 1.0f) * g_sweep;
		const auto c = knob.getCentre();
		const auto r = d * 0.5f;
		_g.setColour(juce::Colours::white);
		_g.drawLine(c.x + std::sin(angle) * r * 0.25f, c.y - std::cos(angle) * r * 0.25f,
			c.x + std::sin(angle) * r * 0.9f, c.y - std::cos(angle) * r * 0.9f, 3.0f);
		_g.setColour(g_ink);
		_g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
		_g.drawText(m_label, labelArea, juce::Justification::centred);
	}

	MicroPanel::MicroPanel(PanelHost& _host) : m_host(_host), m_knobs{{MicroKnob({}, 255), MicroKnob({}, 0), MicroKnob({}, 0), MicroKnob({}, 0)}}
	{
		setSize(Width, Height);
		auto& mc = m_host.mc();
		// The volume's entry is the OS's; the edit knobs 1-3 are the first three.
		static constexpr std::array<uint8_t, 4> entry{{g1::g_qspiVolume, 0, 1, 2}};
		for(size_t i = 0; i < m_knobs.size(); ++i)
		{
			m_knobs[i].setValue(mc.qspiAdc(entry[i]), false);
			m_knobs[i].onChange = [this, i](const int _v) { m_host.mc().setQspiAdc(entry[i], static_cast<uint8_t>(_v)); };
			m_knobs[i].setTooltip(i == 0 ? "The master volume" : "Edit knob " + juce::String(i) + " (what the patch assigned to it)");
			addAndMakeVisible(m_knobs[i]);
		}
		// Shift (also the computer's Shift key), then the two on the right, which select patches.
		static constexpr std::array<uint32_t, 3> bit{{g1::g_microShiftBit, g1::g_microUpperBit, g1::g_microLowerBit}};
		static constexpr std::array<const char*, 3> tip{{
			"Shift (also the computer's Shift key): with it, the knobs are Master Tune, MIDI Channel and Patch Select,\n"
			"the upper button plays a note and the lower one is the patch's switch",
			"The next patch. With Shift: Note Trig, a note while it is held",
			"The previous patch. With Shift: the switch a patch can assign (4)"}};
		for(size_t i = 0; i < m_buttons.size(); ++i)
		{
			m_buttons[i].onHeld = [this, i](const bool _held) { m_host.mc().setMicroButton(bit[i], _held); };
			m_buttons[i].setTooltip(tip[i]);
			addAndMakeVisible(m_buttons[i]);
		}
		m_settings.setTooltip(m_host.settingsTooltip());
		m_settings.onClick = [this]
		{
			if(auto content = m_host.createSettings())
				juce::CallOutBox::launchAsynchronously(std::move(content), m_settings.getScreenBounds(), nullptr);
		};
		addAndMakeVisible(m_settings);
		m_status.setColour(juce::Label::textColourId, g_ink);
		m_status.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
		addAndMakeVisible(m_status);
		startTimerHz(30);
	}

	MicroPanel::~MicroPanel()
	{
		for(auto& b : m_buttons)	// nothing stays pressed in the G1 once its panel goes
			b.onHeld = nullptr;
		for(const auto bit : {g1::g_microShiftBit, g1::g_microUpperBit, g1::g_microLowerBit})
			m_host.mc().setMicroButton(bit, false);
	}

	void MicroPanel::timerCallback()
	{
		auto& mc = m_host.mc();
		// The computer's Shift key holds the box's Shift, read from the system so it works with the
		// mouse over the panel without the focus (a plugin window), as on the G1's panel.
		m_buttons[0].setKeyHeld(juce::ModifierKeys::getCurrentModifiersRealtime().isShiftDown() && (hasKeyboardFocus(true) || isMouseOver(true)));
		const std::array<uint8_t, 2> now{{mc.microDigit(0), mc.microDigit(1)}};
		if(now != m_digits)
		{
			m_digits = now;
			repaint();
		}
		static int tick = 0;
		if(++tick % 15 == 0)
		{
			const auto s = m_host.stats();
			m_status.setText(juce::String::formatted("Micro Modular, 1 DSP   speed %.0f%%  load %.0f%%  cpu %.1f cores", s.speed, s.load, s.cpuCores),
				juce::dontSendNotification);
		}
	}

	void MicroPanel::resized()
	{
		const auto p = getLocalBounds().toFloat();
		m_settings.setBounds(at(p, 0.835f, 0.215f, 0.12f, 0.06f).toNearestInt());
		m_status.setBounds(at(p, 0.04f, 0.93f, 0.92f, 0.05f).toNearestInt());
		// Shift, Volume and knobs 1-3 along the front, under their labels.
		m_buttons[0].setBounds(at(p, 0.065f, 0.765f, 0.085f, 0.075f).toNearestInt());
		m_knobs[0].setBounds(at(p, 0.17f, 0.71f, 0.09f, 0.18f).toNearestInt());
		for(size_t i = 1; i < 4; ++i)
		{
			const auto& c = g_columns[i - 1];
			m_knobs[i].setBounds(at(p, c.x + c.w / 2 - 0.045f, 0.71f, 0.09f, 0.18f).toNearestInt());
		}
		// The two buttons on the right, one over the other.
		m_buttons[1].setBounds(at(p, 0.84f, 0.705f, 0.085f, 0.075f).toNearestInt());
		m_buttons[2].setBounds(at(p, 0.84f, 0.81f, 0.085f, 0.075f).toNearestInt());
	}

	void MicroPanel::drawDigit(juce::Graphics& _g, const juce::Rectangle<float> _r, const uint8_t _segments) const
	{
		// A segment is lit when its bit is low: a, b, c, d, e, f, g are bits 0-6, the dot bit 7.
		const auto lit = static_cast<uint8_t>(~_segments);
		const float t = _r.getWidth() * 0.16f, w = _r.getWidth(), h = _r.getHeight();
		auto seg = [&](const int _bit, const juce::Rectangle<float> _s)
		{
			_g.setColour((lit >> _bit) & 1 ? g_lit : g_unlit);
			_g.fillRoundedRectangle(_s, t * 0.4f);
		};
		const float x = _r.getX(), y = _r.getY(), hh = h * 0.5f;
		seg(0, {x + t, y, w - 2 * t, t});							// a
		seg(1, {x + w - t, y + t, t, hh - t * 1.5f});				// b
		seg(2, {x + w - t, y + hh + t * 0.5f, t, hh - t * 1.5f});	// c
		seg(3, {x + t, y + h - t, w - 2 * t, t});					// d
		seg(4, {x, y + hh + t * 0.5f, t, hh - t * 1.5f});			// e
		seg(5, {x, y + t, t, hh - t * 1.5f});						// f
		seg(6, {x + t, y + hh - t * 0.5f, w - 2 * t, t});			// g
		_g.setColour((lit >> 7) & 1 ? g_lit : g_unlit);
		_g.fillEllipse(x + w + t * 0.3f, y + h - t, t, t);
	}

	void MicroPanel::paint(juce::Graphics& _g)
	{
		const auto p = getLocalBounds().toFloat();
		_g.fillAll(g_body);

		// The plum band with the model's name, and its sweep down on the right.
		juce::Path band;
		const auto b = at(p, 0.0f, 0.19f, 1.0f, 0.36f);
		band.startNewSubPath(b.getX(), b.getY());
		band.lineTo(b.getRight(), b.getY());
		band.lineTo(b.getRight(), b.getBottom());
		band.lineTo(b.getX() + 0.08f * b.getWidth(), b.getBottom());
		band.quadraticTo(b.getX(), b.getBottom(), b.getX(), b.getBottom() - 0.25f * b.getHeight());
		band.closeSubPath();
		_g.setColour(g_plum);
		_g.fillPath(band);
		_g.setColour(g_body);
		_g.setFont(juce::FontOptions(34.0f, juce::Font::bold));
		_g.drawText("MICRO MODULAR", at(p, 0.05f, 0.24f, 0.6f, 0.09f), juce::Justification::centredLeft);

		// The labels over the controls: Shift and Volume, then each column's number over what Shift
		// makes of it.
		labelBox(_g, at(p, 0.06f, 0.655f, 0.1f, 0.05f), "Shift", true, 15.0f);
		labelBox(_g, at(p, 0.16f, 0.655f, 0.11f, 0.05f), "Volume", false, 15.0f);
		for(const auto& c : g_columns)
		{
			labelBox(_g, at(p, c.x, 0.605f, c.w, 0.05f), c.top, false, 15.0f);
			labelBox(_g, at(p, c.x, 0.655f, c.w, 0.05f), c.bottom, *c.top != 0, 15.0f);
		}

		// The display's glass, under "Patch": the digit the OS lights second (its 2) is the left one.
		auto display = at(p, 0.725f, 0.72f, 0.08f, 0.15f);
		_g.setColour(g_glass);
		_g.fillRoundedRectangle(display, 4.0f);
		display = display.reduced(display.getWidth() * 0.12f, display.getHeight() * 0.14f);
		const float dw = display.getWidth() / 2.0f;
		drawDigit(_g, {display.getX(), display.getY(), dw - 7.0f, display.getHeight()}, m_digits[1]);
		drawDigit(_g, {display.getX() + dw + 2.0f, display.getY(), dw - 7.0f, display.getHeight()}, m_digits[0]);
	}
}
