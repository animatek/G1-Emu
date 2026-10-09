#pragma once

// What the panel shows over itself, in one look: a card over the synth, what is behind it blurred
// and showing through, the synth around it darkened. ConfirmView uses it: the panel's own question
// before something that cannot be undone (Restart). AboutView too: the credits and the licenses.
// And CardView, for what the host shows there (Settings).

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace g1gui
{
	namespace overlay
	{
		const juce::Colour Body(0xff1c1c20), Rule(0xff35353c), NoteText(0xffc8ccd0);
		constexpr int Margin = 24, TitleRuleY = 44;
		constexpr float TextSize = 13.0f, HeadingSize = 15.0f;

		// _behind photographed small and blurred, for the card's backdrop.
		juce::Image backdrop(juce::Component& _behind);
		// The darkened synth (its top _faceHeight), the card with the backdrop through it, its border,
		// and the title with its rule. _area is the whole overlay, the size of what is behind.
		void paintCard(juce::Graphics& _g, juce::Rectangle<int> _area, const juce::Image& _backdrop, int _faceHeight,
			juce::Rectangle<int> _card, const juce::String& _title);
	}

	// A question over the panel, with a button that does it and one that does not. Escape, Cancel
	// or a click beside the card leave it; Return answers yes.
	class ConfirmView : public juce::Component
	{
	public:
		ConfirmView();

		// Shows it over _behind (its parent), centred in _space; _onYes runs once it is closed.
		void open(juce::Component& _behind, int _faceHeight, juce::Rectangle<int> _space, const juce::String& _title,
			const juce::String& _text, const juce::String& _yes, std::function<void()> _onYes);
		void close();
		std::function<void()> onClose;

		void paint(juce::Graphics& _g) override;
		void resized() override;
		void mouseDown(const juce::MouseEvent& _e) override;
		bool keyPressed(const juce::KeyPress& _key) override;

	private:
		juce::Rectangle<int> card() const;
		void answer(bool _yes);

		juce::Image m_backdrop;
		int m_faceHeight = 0;
		juce::Rectangle<int> m_space;
		juce::String m_title;
		std::function<void()> m_onYes;
		juce::Label m_text;
		juce::TextButton m_yes, m_cancel{"Cancel"};
	};

	// A card over the panel holding what the host gives it (Settings): the content at the size it
	// sets itself, under the title, with Close at the bottom right, over the content's corner,
	// which leaves CloseSpace free for it. Escape, Close or a click beside the card leave it, and
	// the content goes with it.
	class CardView : public juce::Component
	{
	public:
		static constexpr int CloseW = 90, CloseH = 26;
		static constexpr int CloseSpace = CloseW + 12;	// what the content's bottom row leaves at its right

		CardView();

		// Shows _content over _behind (its parent), centred in _space.
		void open(juce::Component& _behind, int _faceHeight, juce::Rectangle<int> _space, const juce::String& _title,
			std::unique_ptr<juce::Component> _content);
		void close();
		std::function<void()> onClose;

		void paint(juce::Graphics& _g) override;
		void resized() override;
		void mouseDown(const juce::MouseEvent& _e) override;
		bool keyPressed(const juce::KeyPress& _key) override;

	private:
		juce::Rectangle<int> card() const;

		juce::Image m_backdrop;
		int m_faceHeight = 0;
		juce::Rectangle<int> m_space;
		juce::String m_title;
		std::unique_ptr<juce::Component> m_content;
		juce::TextButton m_close{"Close"};
	};

	// About G1-Emu, over the panel: what it is, how Animatek NME finds it, who made it and the
	// license of every part in it. Escape, Close or a click beside the card leave it.
	class AboutView : public juce::Component
	{
	public:
		AboutView();

		// Shows it over _behind (its parent), centred in _space.
		void open(juce::Component& _behind, int _faceHeight, juce::Rectangle<int> _space);
		void close();
		std::function<void()> onClose;

		void paint(juce::Graphics& _g) override;
		void resized() override;
		void mouseDown(const juce::MouseEvent& _e) override;
		bool keyPressed(const juce::KeyPress& _key) override;

	private:
		juce::Rectangle<int> card() const;

		juce::Image m_backdrop;
		int m_faceHeight = 0;
		juce::Rectangle<int> m_space;
		juce::TextEditor m_text;
		juce::TextButton m_nme{"Animatek NME"}, m_source{"Source code"}, m_close{"Close"};
	};
}
