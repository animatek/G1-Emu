#include "MicroPanel.h"

#include "g1Lib/g1mc.h"

#include <algorithm>
#include <cmath>

namespace g1gui
{
	namespace
	{
		const juce::Colour g_body(0xffc8322b), g_strip(0xff2a2a4a), g_ink(0xfff0e6df);
		const juce::Colour g_lit(0xffff3b2e), g_unlit(0xff4a1210), g_glass(0xff1c0707);
		constexpr float g_sweep = 0.75f * juce::MathConstants<float>::pi;	// the knob turns +-135 degrees
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
		auto labelArea = area.removeFromBottom(22.0f);
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

	MicroPanel::MicroPanel(PanelHost& _host) : m_host(_host), m_knobs{{MicroKnob("Volume", 255), MicroKnob("1", 0), MicroKnob("2", 0), MicroKnob("3", 0)}}
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

	MicroPanel::~MicroPanel() = default;

	void MicroPanel::timerCallback()
	{
		auto& mc = m_host.mc();
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
		auto area = getLocalBounds().reduced(24);
		auto top = area.removeFromTop(34);
		m_settings.setBounds(top.removeFromRight(110));
		area.removeFromTop(20);
		auto bottom = area.removeFromBottom(24);
		m_status.setBounds(bottom);
		// The display on the right of the knobs, as on the box: volume, then 1-3 across, the display last.
		auto knobs = area.removeFromLeft(area.getWidth() * 2 / 3);
		const auto w = knobs.getWidth() / 4;
		for(auto& k : m_knobs)
			k.setBounds(knobs.removeFromLeft(w).reduced(6, 30));
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
		_g.fillAll(g_body);
		auto area = getLocalBounds().reduced(24);
		area.removeFromTop(54);
		area.removeFromBottom(24);
		_g.setColour(g_strip);
		_g.fillRoundedRectangle(area.toFloat(), 10.0f);
		_g.setColour(g_ink);
		_g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
		_g.drawText("MICRO MODULAR", 24, 22, 300, 30, juce::Justification::centredLeft);

		// The display's glass, on the right: the digit the OS lights second (its 2) is the left one.
		auto display = area.removeFromRight(area.getWidth() / 3).toFloat().reduced(18.0f, 40.0f);
		_g.setColour(g_glass);
		_g.fillRoundedRectangle(display, 6.0f);
		display = display.reduced(14.0f);
		const float dw = (display.getWidth() - 20.0f) / 2.0f;
		drawDigit(_g, {display.getX(), display.getY(), dw - 8.0f, display.getHeight()}, m_digits[1]);
		drawDigit(_g, {display.getX() + dw + 20.0f, display.getY(), dw - 8.0f, display.getHeight()}, m_digits[0]);
	}
}
