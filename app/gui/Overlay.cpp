#include "Overlay.h"

#include <algorithm>
#include <vector>

namespace g1gui
{
	namespace
	{
		constexpr float g_faceDim = 0.5f, g_bodyAlpha = 0.7f;	// the synth darkened around the card, which lets it through
		constexpr float g_backdropScale = 0.25f;	// the panel is photographed small and blurred: cheap, and soft
		constexpr int g_confirmW = 480, g_confirmH = 170;
	}

	juce::Image overlay::backdrop(juce::Component& _behind)
	{
		if(_behind.getLocalBounds().isEmpty())
			return {};
		auto image = _behind.createComponentSnapshot(_behind.getLocalBounds(), true, g_backdropScale);
		juce::ImageConvolutionKernel blur(7);
		blur.createGaussianBlur(2.5f);
		const auto sharp = image.createCopy();
		blur.applyToImage(image, sharp, image.getBounds());
		return image;
	}

	// Only what is behind the card is blurred, and shows through its body; the synth around it is
	// darkened, and the rest of the panel stays as it is.
	void overlay::paintCard(juce::Graphics& _g, const juce::Rectangle<int> _area, const juce::Image& _backdrop, const int _faceHeight,
		const juce::Rectangle<int> _card, const juce::String& _title)
	{
		juce::Path shape;
		shape.addRoundedRectangle(_card.toFloat(), 10.0f);
		if(_backdrop.isValid())
		{
			juce::Graphics::ScopedSaveState state(_g);
			_g.reduceClipRegion(shape);
			_g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
			_g.drawImage(_backdrop, _area.toFloat());
		}
		_g.setColour(juce::Colours::black.withAlpha(g_faceDim));
		_g.fillRect(_area.withHeight(_faceHeight));
		_g.setColour(Body.withAlpha(g_bodyAlpha));
		_g.fillPath(shape);
		_g.setColour(Rule);
		_g.drawRoundedRectangle(_card.toFloat().reduced(0.5f), 10.0f, 1.0f);
		_g.drawLine(static_cast<float>(_card.getX() + Margin), static_cast<float>(_card.getY() + TitleRuleY),
			static_cast<float>(_card.getRight() - Margin), static_cast<float>(_card.getY() + TitleRuleY));

		_g.setColour(juce::Colours::white);
		_g.setFont(juce::FontOptions(HeadingSize, juce::Font::bold));
		_g.drawText(_title, _card.getX() + Margin, _card.getY() + 12, _card.getWidth() - 2 * Margin, 22, juce::Justification::centredLeft);
	}

	ConfirmView::ConfirmView()
	{
		setWantsKeyboardFocus(true);
		m_text.setFont(juce::FontOptions(overlay::TextSize));
		m_text.setColour(juce::Label::textColourId, overlay::NoteText);
		m_text.setJustificationType(juce::Justification::topLeft);
		addAndMakeVisible(m_text);
		m_yes.onClick = [this] { answer(true); };
		m_cancel.onClick = [this] { answer(false); };
		addAndMakeVisible(m_yes);
		addAndMakeVisible(m_cancel);
	}

	void ConfirmView::open(juce::Component& _behind, const int _faceHeight, const juce::Rectangle<int> _space, const juce::String& _title,
		const juce::String& _text, const juce::String& _yes, std::function<void()> _onYes)
	{
		m_backdrop = overlay::backdrop(_behind);
		m_faceHeight = _faceHeight;
		m_space = _space;
		m_title = _title.toUpperCase();
		m_text.setText(_text, juce::dontSendNotification);
		m_yes.setButtonText(_yes);
		m_onYes = std::move(_onYes);
		setBounds(_behind.getLocalBounds());
		resized();
		setVisible(true);
		toFront(true);
		grabKeyboardFocus();
	}

	void ConfirmView::close()
	{
		setVisible(false);
		m_backdrop = {};
		if(onClose)
			onClose();
	}

	void ConfirmView::answer(const bool _yes)
	{
		auto yes = std::move(m_onYes);
		m_onYes = nullptr;
		close();
		if(_yes && yes)
			yes();
	}

	juce::Rectangle<int> ConfirmView::card() const
	{
		const auto space = m_space.isEmpty() ? getLocalBounds() : m_space;
		return juce::Rectangle<int>(g_confirmW, g_confirmH).withCentre(space.getCentre());
	}

	void ConfirmView::paint(juce::Graphics& _g)
	{
		overlay::paintCard(_g, getLocalBounds(), m_backdrop, m_faceHeight, card(), m_title);
	}

	// The text under the title, the buttons at the bottom right: Cancel last, as Close is on the
	// Synth Settings.
	void ConfirmView::resized()
	{
		const auto c = card();
		const int m = overlay::Margin;
		m_text.setBounds(c.getX() + m - 4, c.getY() + overlay::TitleRuleY + 10, c.getWidth() - 2 * m + 8, 60);
		m_cancel.setBounds(c.getRight() - m - 90, c.getBottom() - 18 - 26, 90, 26);
		m_yes.setBounds(m_cancel.getX() - 10 - 90, m_cancel.getY(), 90, 26);
	}

	void ConfirmView::mouseDown(const juce::MouseEvent& _e)
	{
		if(!card().contains(_e.getPosition()))
			answer(false);
	}

	bool ConfirmView::keyPressed(const juce::KeyPress& _key)
	{
		if(_key == juce::KeyPress::escapeKey)
			answer(false);
		else if(_key == juce::KeyPress::returnKey)
			answer(true);
		else
			return false;
		return true;
	}
}

namespace g1gui
{
	namespace
	{
		constexpr int g_cardGap = 12, g_cardBottom = 18;	// between the title's rule and the content; below the content
	}

	CardView::CardView()
	{
		setWantsKeyboardFocus(true);
		m_close.onClick = [this] { close(); };
		addAndMakeVisible(m_close);
	}

	void CardView::open(juce::Component& _behind, const int _faceHeight, const juce::Rectangle<int> _space, const juce::String& _title,
		std::unique_ptr<juce::Component> _content)
	{
		m_backdrop = overlay::backdrop(_behind);
		m_faceHeight = _faceHeight;
		m_space = _space;
		m_title = _title.toUpperCase();
		m_content = std::move(_content);
		if(m_content)
			addAndMakeVisible(*m_content);
		setBounds(_behind.getLocalBounds());
		resized();
		setVisible(true);
		toFront(true);
		m_close.toFront(false);
		grabKeyboardFocus();
	}

	void CardView::close()
	{
		setVisible(false);
		m_backdrop = {};
		m_content.reset();	// it goes with the card: its timers and windows too
		if(onClose)
			onClose();
	}

	// Around the content at its own size, or as the space allows on a small panel.
	juce::Rectangle<int> CardView::card() const
	{
		const auto space = m_space.isEmpty() ? getLocalBounds() : m_space;
		const int w = (m_content ? m_content->getWidth() : 0) + 2 * overlay::Margin;
		const int h = overlay::TitleRuleY + g_cardGap + (m_content ? m_content->getHeight() : 0) + g_cardBottom;
		return juce::Rectangle<int>(std::min(w, space.getWidth() - 16), std::min(h, space.getHeight() - 16)).withCentre(space.getCentre());
	}

	void CardView::paint(juce::Graphics& _g)
	{
		overlay::paintCard(_g, getLocalBounds(), m_backdrop, m_faceHeight, card(), m_title);
	}

	void CardView::resized()
	{
		const auto c = card();
		const int m = overlay::Margin;
		const int top = c.getY() + overlay::TitleRuleY + g_cardGap, bottom = c.getBottom() - g_cardBottom;
		if(m_content)
			m_content->setTopLeftPosition(c.getX() + m, top);
		m_close.setBounds(c.getRight() - m - CloseW, bottom - CloseH, CloseW, CloseH);
	}

	void CardView::mouseDown(const juce::MouseEvent& _e)
	{
		if(!card().contains(_e.getPosition()))
			close();
	}

	bool CardView::keyPressed(const juce::KeyPress& _key)
	{
		if(_key != juce::KeyPress::escapeKey)
			return false;
		close();
		return true;
	}
}

namespace g1gui
{
	namespace
	{
		constexpr int g_aboutW = 640, g_aboutH = 470;

		struct AboutSection
		{
			const char* heading;
			juce::String body;
		};

		// Who made what, and under which license. When a part comes in or a contributor joins,
		// this is the list to keep, with README.md's "License and credits".
		std::vector<AboutSection> aboutSections()
		{
			return {
				{"", juce::String("Version ") + G1_BUILD_VERSION + "\n\n"
					"The Nord Modular G1 rack, emulated: its own OS running on an emulated 68331 and four emulated "
					"DSP56303s, as a standalone program and as a VST3 and CLAP plugin. Free and open source."},
				{"Works with any Nord Modular editor",
					"G1-Emu answers on its PC Port like the real synth, so any Nord Modular editor connects to it, new or "
					"old. Animatek NME, the modern editor for the G1, goes further: it finds G1-Emu by itself and connects "
					"with no MIDI port to set up (the direct link). Open both, and edit G1-Emu as if it were the real synth."},
				{"Credits",
					"Javier Melgar (Animatek): G1-Emu and Animatek NME.\n"
					"Mike Fiction: panel skin and GUI design.\n"
					"The Usual Suspects (dsp56300 and the Gearmulator team): Gearmulator, the DSP56300 and 68k "
					"emulation G1-Emu is built on.\n"
					"joelanders: the Gearmulator fork with the Monomachine and Machinedrum this work started from.\n"
					"Tuth: Windows fixes (PR #5).\n"
					"Psychlist1972: help with Windows MIDI Services.\n\n"
					"Testing and reports: AlphasiaIndustries, artqcid, Garrincha568, JuliusLC, masc4ii, ModGod222, "
					"msavery123, nirsu1, psy-dub, Waltercalling, and everyone who reports on GitHub."},
				{"Licenses",
					"G1-Emu: GNU General Public License v3 (it links Gearmulator). The source is on GitHub.\n"
					"Panel artwork (app/gui/skin and the icon): (c) 2026 Mike Fiction, Creative Commons Attribution "
					"4.0 (CC BY 4.0).\n"
					"Gearmulator's dsp56300 and mc68k: GNU GPL v3.\n"
					"Musashi 68000 core (Karl Stenerud): MIT.\n"
					"AsmJit (the AsmJit Authors): zlib.\n"
					"JUCE 8 (Raw Material Software): GNU AGPL v3.\n"
					"VST 3 SDK (Steinberg Media Technologies): MIT. VST is a trademark of Steinberg Media Technologies GmbH.\n"
					"CLAP and clap-juce-extensions (free-audio): MIT."},
				{"Not affiliated with Clavia",
					"Nord and Nord Modular are trademarks of Clavia DMI AB, which neither makes, endorses nor supports "
					"G1-Emu. G1-Emu ships no ROM: it runs the one from your own synth."},
			};
		}
	}

	AboutView::AboutView()
	{
		setWantsKeyboardFocus(true);
		m_text.setMultiLine(true, true);
		m_text.setReadOnly(true);
		m_text.setScrollbarsShown(true);
		m_text.setCaretVisible(false);
		m_text.setPopupMenuEnabled(false);
		m_text.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
		m_text.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
		m_text.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
		for(const auto& s : aboutSections())
		{
			if(*s.heading)
			{
				m_text.setFont(juce::FontOptions(overlay::HeadingSize, juce::Font::bold));
				m_text.setColour(juce::TextEditor::textColourId, juce::Colours::white);
				m_text.insertTextAtCaret(juce::String("\n") + s.heading + "\n");
			}
			m_text.setFont(juce::FontOptions(overlay::TextSize));
			m_text.setColour(juce::TextEditor::textColourId, overlay::NoteText);
			m_text.insertTextAtCaret(s.body + "\n");
		}
		m_text.moveCaretToTop(false);
		addAndMakeVisible(m_text);

		m_nme.setTooltip("Animatek NME on GitHub");
		m_nme.onClick = [] { juce::URL("https://github.com/animatek/Animatek-NME").launchInDefaultBrowser(); };
		m_source.setTooltip("G1-Emu's source code and its licenses on GitHub");
		m_source.onClick = [] { juce::URL("https://github.com/animatek/G1-Emu").launchInDefaultBrowser(); };
		m_close.onClick = [this] { close(); };
		for(auto* b : {&m_nme, &m_source, &m_close})
			addAndMakeVisible(*b);
	}

	void AboutView::open(juce::Component& _behind, const int _faceHeight, const juce::Rectangle<int> _space)
	{
		m_backdrop = overlay::backdrop(_behind);
		m_faceHeight = _faceHeight;
		m_space = _space;
		setBounds(_behind.getLocalBounds());
		resized();
		m_text.moveCaretToTop(false);
		setVisible(true);
		toFront(true);
		grabKeyboardFocus();
	}

	void AboutView::close()
	{
		setVisible(false);
		m_backdrop = {};
		if(onClose)
			onClose();
	}

	// As large as it is meant to be, or as the space allows on a small panel.
	juce::Rectangle<int> AboutView::card() const
	{
		const auto space = m_space.isEmpty() ? getLocalBounds() : m_space;
		return juce::Rectangle<int>(std::min(g_aboutW, space.getWidth() - 16), std::min(g_aboutH, space.getHeight() - 16))
			.withCentre(space.getCentre());
	}

	void AboutView::paint(juce::Graphics& _g)
	{
		overlay::paintCard(_g, getLocalBounds(), m_backdrop, m_faceHeight, card(), "ABOUT G1-EMU");
	}

	// The text between the title and the buttons, which sit at the bottom: the links at the left,
	// Close at the right.
	void AboutView::resized()
	{
		const auto c = card();
		const int m = overlay::Margin;
		const int buttonsY = c.getBottom() - 18 - 26;
		m_text.setBounds(c.getX() + m - 6, c.getY() + overlay::TitleRuleY + 4, c.getWidth() - 2 * m + 12, buttonsY - 10 - (c.getY() + overlay::TitleRuleY + 4));
		m_nme.setBounds(c.getX() + m, buttonsY, 120, 26);
		m_source.setBounds(m_nme.getRight() + 10, buttonsY, 110, 26);
		m_close.setBounds(c.getRight() - m - 90, buttonsY, 90, 26);
	}

	void AboutView::mouseDown(const juce::MouseEvent& _e)
	{
		if(!card().contains(_e.getPosition()))
			close();
	}

	bool AboutView::keyPressed(const juce::KeyPress& _key)
	{
		if(_key != juce::KeyPress::escapeKey)
			return false;
		close();
		return true;
	}
}
