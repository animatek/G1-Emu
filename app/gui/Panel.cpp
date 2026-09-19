#include "Panel.h"

#include <cmath>

namespace g1gui
{
	namespace
	{
		// Mandos 1-18: canal del multiplexor del ADC. El OS los lee en el orden de su tabla
		// ($14420A) y los guarda en ese orden; se supone que el mando n es la entrada n-1.
		// Falta comprobarlo con el aparato.
		constexpr std::array<uint8_t, 18> g_knobAdc = {0x31, 0x37, 0x2d, 0x32, 0x28, 0x2e, 0x33, 0x29, 0x2f, 0x34, 0x2a, 0x1a, 0x35, 0x2b, 0x1b, 0x36, 0x2c, 0x1c};
		constexpr uint8_t g_volumeAdc = 0x30;

		// Botones identificados pulsandolos uno a uno (G1_PROBE); los demas, sin conectar.
		constexpr ButtonBit g_btnA{0, 2}, g_btnB{0, 3}, g_btnC{0, 4}, g_btnD{0, 5};
		constexpr ButtonBit g_btnStore{0, 6}, g_btnSystem{0, 7}, g_btnAssign{1, 2};
		constexpr ButtonBit g_unknown{};

		// LEDs identificados: los de los slots, bit 7 de cada fila (activos a nivel bajo).
		constexpr std::array<LedBit, 4> g_slotLeds = {LedBit{0, 7}, LedBit{1, 7}, LedBit{2, 7}, LedBit{3, 7}};

		const juce::Colour g_chassis(0xffb5202c), g_face(0xff2c2b4a), g_panel(0xffd6d6d2), g_groupLine(0xffe8a33a);
	}

	// ________________________________________________________________________
	// Pantalla: caracteres del HD44780; los propios (codigos 0-15) se dibujan desde la CGRAM.

	void LcdView::paint(juce::Graphics& _g)
	{
		const auto area = getLocalBounds().toFloat();
		_g.setColour(juce::Colour(0xff1a1a1a));
		_g.fillRoundedRectangle(area, 6.0f);
		const auto glass = area.reduced(6.0f);
		_g.setColour(m_lcd.displayOn() ? juce::Colour(0xff9fd33a) : juce::Colour(0xff6f8f32));
		_g.fillRoundedRectangle(glass, 3.0f);
		if(!m_lcd.displayOn())
			return;

		constexpr int cols = 16, rows = 2;
		const float cellW = (glass.getWidth() - 12.0f) / cols, cellH = (glass.getHeight() - 10.0f) / rows;
		const auto cg = m_lcd.cgram();
		const auto ink = juce::Colour(0xff1d2a10);
		_g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), cellH * 0.82f, juce::Font::bold));
		for(int r = 0; r < rows; ++r)
		{
			const auto text = m_lcd.line(static_cast<uint32_t>(r), cols);
			for(int c = 0; c < cols && c < static_cast<int>(text.size()); ++c)
			{
				const auto ch = static_cast<uint8_t>(text[static_cast<size_t>(c)]);
				const juce::Rectangle<float> cell(glass.getX() + 6.0f + c * cellW, glass.getY() + 5.0f + r * cellH, cellW, cellH);
				if(ch < 16)
				{
					// 5x8 puntos desde la CGRAM
					const auto base = static_cast<size_t>(ch & 7) * 8;
					const float px = cell.getWidth() / 6.0f, py = cell.getHeight() / 8.5f;
					_g.setColour(ink);
					for(int y = 0; y < 8; ++y)
						for(int x = 0; x < 5; ++x)
							if(cg[base + static_cast<size_t>(y)] & (0x10 >> x))
								_g.fillRect(cell.getX() + x * px + px * 0.5f, cell.getY() + y * py, px * 0.9f, py * 0.9f);
				}
				else
				{
					_g.setColour(ink);
					_g.drawText(juce::String::charToString(static_cast<juce::juce_wchar>(ch)), cell, juce::Justification::centred, false);
				}
			}
		}
	}

	void LedView::paint(juce::Graphics& _g)
	{
		const auto r = getLocalBounds().toFloat().reduced(1.0f);
		_g.setColour(m_on ? juce::Colour(0xff4dff6a) : juce::Colour(0xff1f4a26));
		_g.fillEllipse(r);
		_g.setColour(juce::Colours::black.withAlpha(0.6f));
		_g.drawEllipse(r, 1.0f);
	}

	PanelButton::PanelButton(const juce::String& _name, g1::Microcontroller& _mc, const ButtonBit _bit)
		: juce::TextButton(_name), m_mc(_mc), m_bit(_bit)
	{
		setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a2a2a));
		setColour(juce::TextButton::textColourOffId, juce::Colours::white);
		if(!_bit.known())
		{
			setEnabled(false);
			setTooltip("Sin identificar todavia en la matriz del panel");
			return;
		}
		setTooltip("Fila " + juce::String(_bit.row) + ", bit " + juce::String(_bit.bit));
		onStateChange = [this]
		{
			const bool down = isDown();
			if(down != m_down)
			{
				m_down = down;
				m_mc.setButton(static_cast<uint32_t>(m_bit.row), static_cast<uint32_t>(m_bit.bit), down);
			}
		};
	}

	// ________________________________________________________________________

	Panel::Panel(g1app::EmuHost& _host) : m_host(_host), m_mc(_host.mc()), m_lcd(_host.mc().getLcd())
	{
		addAndMakeVisible(m_lcd);

		auto setupKnob = [this](juce::Slider& _s, const uint8_t _adc, const double _initial, const juce::String& _tip)
		{
			_s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
			_s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
			_s.setRange(0, 255, 1);
			_s.setRotaryParameters(juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
			_s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xffd23b3b));
			_s.setColour(juce::Slider::thumbColourId, juce::Colours::white);
			_s.setTooltip(_tip);
			_s.setValue(_initial, juce::dontSendNotification);
			_s.onValueChange = [this, &_s, _adc] { m_mc.setAdc(_adc, static_cast<uint8_t>(_s.getValue())); };
			m_mc.setAdc(_adc, static_cast<uint8_t>(_initial));
			addAndMakeVisible(_s);
		};
		setupKnob(m_volume, g_volumeAdc, 255, "Master volume (el OS lo lee al encender y al moverlo)");
		for(size_t i = 0; i < m_knobs.size(); ++i)
		{
			setupKnob(m_knobs[i], g_knobAdc[i], 0, "Mando " + juce::String(static_cast<int>(i + 1)));
			addAndMakeVisible(m_knobLeds[i]);
		}

		m_panelSplit = &addButton("Panel Split", g_unknown);
		m_find = &addButton("Find", g_unknown);
		m_octDown = &addButton("<", g_unknown);
		m_octUp = &addButton(">", g_unknown);
		for(auto& l : m_octLeds)
			l = &addLed({});
		const char* modes[] = {"Store", "System", "Edit", "Patch/Load"};
		const ButtonBit modeBits[] = {g_btnStore, g_btnSystem, g_unknown, g_unknown};
		for(size_t i = 0; i < 4; ++i)
		{
			m_modeButtons[i] = &addButton(modes[i], modeBits[i]);
			m_modeLeds[i] = &addLed({});
		}
		const char* slots[] = {"A", "B", "C", "D"};
		const ButtonBit slotBits[] = {g_btnA, g_btnB, g_btnC, g_btnD};
		for(size_t i = 0; i < 4; ++i)
		{
			m_slotButtons[i] = &addButton(slots[i], slotBits[i]);
			m_slotLeds[i] = &addLed(g_slotLeds[i]);
		}
		m_assign = &addButton("Assign / Morph", g_btnAssign);
		m_shift = &addButton("Shift", g_unknown);
		const char* nav[] = {"<", "^", "v", ">"};
		for(size_t i = 0; i < 4; ++i)
			m_nav[i] = &addButton(nav[i], g_unknown);

		// Matrices en crudo
		for(int i = 0; i < 24; ++i)
		{
			m_rawButtons[static_cast<size_t>(i)] = std::make_unique<PanelButton>(juce::String(i / 8) + "." + juce::String(i % 8), m_mc, ButtonBit{i / 8, i % 8});
			addChildComponent(*m_rawButtons[static_cast<size_t>(i)]);
		}
		for(auto& l : m_rawLeds)
			addChildComponent(l);
		m_showMatrix.onClick = [this]
		{
			const bool show = m_showMatrix.getToggleState();
			for(auto& b : m_rawButtons) b->setVisible(show);
			for(auto& l : m_rawLeds) l.setVisible(show);
			setSize(getWidth(), show ? 590 : 470);	// la ventana sigue al contenido
			repaint();
		};
		addAndMakeVisible(m_showMatrix);

		m_status.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
		m_status.setColour(juce::Label::textColourId, juce::Colours::white);
		addAndMakeVisible(m_status);

		setSize(1200, 470);
		startTimerHz(30);
	}

	PanelButton& Panel::addButton(const juce::String& _name, const ButtonBit _bit)
	{
		m_buttons.push_back(std::make_unique<PanelButton>(_name, m_mc, _bit));
		addAndMakeVisible(*m_buttons.back());
		return *m_buttons.back();
	}

	LedView& Panel::addLed(const LedBit _bit)
	{
		m_leds.push_back(std::make_unique<LedView>());
		addAndMakeVisible(*m_leds.back());
		if(_bit.known())
			m_ledMap.emplace_back(m_leds.back().get(), _bit);
		return *m_leds.back();
	}

	void Panel::paint(juce::Graphics& _g)
	{
		_g.fillAll(g_chassis);
		const auto face = juce::Rectangle<float>(10.0f, 10.0f, static_cast<float>(getWidth()) - 20.0f, 400.0f);
		_g.setColour(g_face);
		_g.fillRoundedRectangle(face, 14.0f);

		// Grupos de mandos y la zona de la pantalla, en gris como el aparato
		const juce::Rectangle<float> groups[] = {{130, 26, 200, 340}, {345, 26, 200, 340}, {560, 26, 100, 340}, {675, 26, 100, 340}, {795, 26, 385, 340}};
		for(const auto& g : groups)
		{
			_g.setColour(g_panel);
			_g.fillRoundedRectangle(g, 10.0f);
			_g.setColour(g_groupLine);
			_g.drawRoundedRectangle(g, 10.0f, 2.0f);
		}

		_g.setColour(juce::Colours::white);
		_g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
		_g.drawText("MASTER VOLUME", 20, 24, 100, 16, juce::Justification::centred);
		_g.drawText("Oct Shift", 20, 300, 100, 14, juce::Justification::centred);
		_g.setColour(juce::Colour(0xff333333));
		for(size_t i = 0; i < m_knobs.size(); ++i)
		{
			const auto k = m_knobs[i].getBounds();
			_g.drawText(juce::String(static_cast<int>(i + 1)), k.getRight() - 22, k.getY() - 15, 18, 14, juce::Justification::centredRight);
		}

		_g.setColour(juce::Colours::white.withAlpha(0.7f));
		_g.setFont(juce::FontOptions(11.0f));
		_g.drawText("VIRTUAL  MODULAR  SYNTHESIZER   -   G1-Emu", face.withTop(378).withHeight(20).toNearestInt(), juce::Justification::centred);

		if(m_showMatrix.getToggleState())
		{
			_g.setColour(juce::Colours::white);
			_g.drawText("Botones (fila.bit)", 20, 470, 200, 14, juce::Justification::left);
			_g.drawText("LEDs (fila 0-3, bit 7-0; encendido = bit a 0)", 700, 470, 400, 14, juce::Justification::left);
		}
	}

	void Panel::resized()
	{
		m_volume.setBounds(35, 42, 70, 70);
		m_panelSplit->setBounds(22, 150, 96, 26);
		m_find->setBounds(22, 200, 96, 26);
		for(size_t i = 0; i < m_octLeds.size(); ++i)
			m_octLeds[i]->setBounds(30 + static_cast<int>(i) * 17, 318, 12, 12);
		m_octDown->setBounds(28, 338, 40, 24);
		m_octUp->setBounds(72, 338, 40, 24);

		// Mandos: 1-6 en dos columnas, 7-12 en dos, 13-15 y 16-18 en una, de tres en tres
		const int colX[] = {150, 245, 365, 460, 580, 695};
		const int rowY[] = {60, 160, 260};
		for(size_t i = 0; i < m_knobs.size(); ++i)
		{
			const int col = static_cast<int>(i / 3), row = static_cast<int>(i % 3);
			m_knobs[i].setBounds(colX[col], rowY[row], 64, 64);
			m_knobLeds[i].setBounds(colX[col] + 58, rowY[row] - 13, 11, 11);
		}

		m_lcd.setBounds(815, 40, 250, 70);
		for(size_t i = 0; i < 4; ++i)
		{
			const int x = 815 + static_cast<int>(i) * 64;
			m_modeLeds[i]->setBounds(x, 150, 10, 10);
			m_modeButtons[i]->setBounds(x, 164, 60, 26);
			m_slotLeds[i]->setBounds(x, 230, 10, 10);
			m_slotButtons[i]->setBounds(x, 244, 60, 26);
		}
		m_assign->setBounds(1080, 150, 90, 26);
		m_shift->setBounds(1080, 190, 90, 26);
		m_nav[0]->setBounds(1080, 70, 28, 26);
		m_nav[1]->setBounds(1110, 40, 28, 26);
		m_nav[2]->setBounds(1110, 100, 28, 26);
		m_nav[3]->setBounds(1140, 70, 28, 26);

		m_showMatrix.setBounds(1090, 420, 90, 22);
		m_status.setBounds(12, 416, 1070, 26);

		for(size_t i = 0; i < m_rawButtons.size(); ++i)
			m_rawButtons[i]->setBounds(20 + static_cast<int>(i % 8) * 70, 490 + static_cast<int>(i / 8) * 30, 64, 26);
		for(size_t i = 0; i < m_rawLeds.size(); ++i)
			m_rawLeds[i].setBounds(700 + static_cast<int>(7 - i % 8) * 24, 492 + static_cast<int>(i / 8) * 22, 14, 14);
	}

	void Panel::timerCallback()
	{
		m_lcd.repaint();
		for(auto& [led, bit] : m_ledMap)
			led->setOn(!(m_mc.ledRow(static_cast<uint32_t>(bit.row)) & (1u << bit.bit)));
		if(m_showMatrix.getToggleState())
			for(size_t i = 0; i < m_rawLeds.size(); ++i)
				m_rawLeds[i].setOn(!(m_mc.ledRow(static_cast<uint32_t>(i / 8)) & (1u << (i % 8))));

		const auto s = m_host.stats();
		m_peakHold = std::max(static_cast<double>(s.peak), m_peakHold * 0.9);
		juce::String dsp;
		for(bool on : s.dspOn)
			dsp << (on ? "o" : "-");
		m_status.setText(juce::String::formatted("velocidad %5.1f%%   carga %3.0f%%   CPU %.1f nucleos   DSP %s   salida 1/2 %s   cortes %llu   |  ",
			s.speed, s.load, s.cpuCores, dsp.toRawUTF8(),
			m_peakHold > 1e-6 ? juce::String::formatted("%+.0f dB", 20.0 * std::log10(m_peakHold)).toRawUTF8() : "silencio",
			static_cast<unsigned long long>(s.xruns)) + juce::String(s.audio), juce::dontSendNotification);
	}
}
