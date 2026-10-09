#include "Editor.h"

#include "Processor.h"

#include "romfinder.h"

namespace g1plugin
{
	Editor::Editor(Processor& _processor) : juce::AudioProcessorEditor(_processor), m_processor(_processor)
	{
		m_message.setColour(juce::Label::textColourId, juce::Colours::white);
		m_message.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
		m_message.setJustificationType(juce::Justification::topLeft);
		addChildComponent(m_message);

		m_openFolder.onClick = []
		{
			const juce::File folder(g1app::publicRomFolder());
			(void)folder.createDirectory();
			folder.revealToUser();
		};
		m_pickRom.onClick = [this] { chooseRom(); };
		addChildComponent(m_openFolder);
		addChildComponent(m_pickRom);

		setResizable(true, true);
		rebuild();
		startTimerHz(5);
	}

	Editor::~Editor()
	{
		stopTimer();
		m_panel.reset();
		m_processor.savePreferences();	// the size it was left at, for the next new instance
	}

	void Editor::engineGoing()
	{
		m_panel.reset();
		m_engine = nullptr;
		m_generation = -1;
	}

	void Editor::timerCallback()
	{
		if(m_generation != m_processor.generation())
			rebuild();
	}

	void Editor::rebuild()
	{
		m_panel.reset();
		m_generation = m_processor.generation();
		m_engine = m_processor.engine();

		const bool noRom = !m_processor.romProblem().empty();
		m_message.setVisible(!m_engine);
		m_openFolder.setVisible(!m_engine && noRom);
		m_pickRom.setVisible(!m_engine && noRom);
		if(!m_engine)
		{
			m_message.setText(noRom ? juce::String(m_processor.romProblem()) : juce::String("Waiting for the host to start the audio..."),
				juce::dontSendNotification);
			// Until the host starts the audio there is nothing to show, and nothing to poll for
			// but the generation: the timer keeps running.
			const float s = std::clamp(panelScale(), g1gui::PanelView::MinScale, g1gui::PanelView::MaxScale);
			fitTo(static_cast<double>(g1gui::Panel::Width) / g1gui::Panel::Height);
			setSize(juce::roundToInt(g1gui::Panel::Width * s), juce::roundToInt(g1gui::Panel::Height * s));
			resized();
			return;
		}
		m_panel = std::make_unique<g1gui::PanelView>(static_cast<g1gui::PanelHost&>(*this));
		m_panel->onAspectChanged = [this] { fitTo(m_panel->aspectRatio()); };
		fitTo(m_panel->aspectRatio());
		addAndMakeVisible(*m_panel);
		setSize(m_panel->getWidth(), m_panel->getHeight());
	}

	void Editor::paint(juce::Graphics& _g)
	{
		_g.fillAll(juce::Colour(g1gui::Panel::FaceColour));
	}

	void Editor::resized()
	{
		if(m_panel)
			m_panel->setBounds(getLocalBounds());	// the host's size or the corner's: the panel scales to it
		auto area = getLocalBounds().reduced(24);
		auto buttons = area.removeFromBottom(32);
		m_pickRom.setBounds(buttons.removeFromRight(200));
		buttons.removeFromRight(12);
		m_openFolder.setBounds(buttons.removeFromRight(200));
		m_message.setBounds(area);
	}

	// Resizable in the panel's proportions, from PanelView's smallest scale to its largest.
	void Editor::fitTo(const double _aspect)
	{
		g1gui::PanelView::applyLimits(*getConstrainer(), _aspect);
	}

	// The panel changes its own height when the extras drawer opens: the editor follows.
	void Editor::childBoundsChanged(juce::Component* _child)
	{
		if(_child == m_panel.get())
			setSize(m_panel->getWidth(), m_panel->getHeight());
	}

	void Editor::chooseRom()
	{
		m_chooser = std::make_unique<juce::FileChooser>("Choose the Nord Modular rack ROM",
			juce::File(g1app::publicRomFolder()), "*.bin;*.BIN");
		m_chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
			[this](const juce::FileChooser& _c)
		{
			const auto file = _c.getResult();
			if(file != juce::File())
				m_processor.useRom(file);	// the generation changes: the timer rebuilds
		});
	}

	g1app::HostStats Editor::stats() { return m_processor.stats(); }
	bool Editor::extrasOpen() const { return m_processor.extrasOpen(); }
	g1app::SynthSettingsLink& Editor::synthSettings() { return m_processor.synthSettings(); }
	g1app::PresetsLink& Editor::presets() { return m_processor.presets(); }
	void Editor::setExtrasOpen(const bool _open) { m_processor.setExtrasOpen(_open); }
	bool Editor::knobDisplays() const { return m_processor.knobDisplays(); }
	void Editor::setKnobDisplays(const bool _on) { m_processor.setKnobDisplays(_on); }
	bool Editor::knobFollowsPatch() const { return m_processor.knobFollowsPatch(); }
	void Editor::setKnobFollowsPatch(const bool _on) { m_processor.setKnobFollowsPatch(_on); }
	uint32_t Editor::randomExcluded() const { return m_processor.randomExcluded(); }
	void Editor::setRandomExcluded(const uint32_t _knobs) { m_processor.setRandomExcluded(_knobs); }
	float Editor::panelScale() const { return m_processor.panelScale(); }
	void Editor::setPanelScale(const float _scale) { m_processor.setPanelScale(_scale); }

	// Later, on the message thread: the restart deletes the panel that asked.
	void Editor::restart()
	{
		juce::MessageManager::callAsync([e = juce::Component::SafePointer<Editor>(this)] { if(e) e->m_processor.restart(); });
	}

	namespace
	{
		// What Settings shows in the plugin, on the panel's card: this instance (its ROM, latency,
		// where its patches came from, its PC Port and direct link) and the notice, as the About
		// card shows its text. Nothing to set: the host and the project decide.
		class InstanceView : public juce::Component
		{
		public:
			explicit InstanceView(const juce::String& _instance)
			{
				m_text.setMultiLine(true, true);
				m_text.setReadOnly(true);
				m_text.setScrollbarsShown(true);
				m_text.setCaretVisible(false);
				m_text.setPopupMenuEnabled(false);
				m_text.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
				m_text.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
				m_text.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
				auto section = [this](const juce::String& _heading, const juce::String& _body)
				{
					m_text.setFont(juce::FontOptions(g1gui::overlay::HeadingSize, juce::Font::bold));
					m_text.setColour(juce::TextEditor::textColourId, juce::Colours::white);
					m_text.insertTextAtCaret(_heading + "\n");
					m_text.setFont(juce::FontOptions(g1gui::overlay::TextSize));
					m_text.setColour(juce::TextEditor::textColourId, g1gui::overlay::NoteText);
					m_text.insertTextAtCaret(_body + "\n\n");
				};
				section("This instance", _instance);
				section("Notice", g1gui::disclaimerText());
				m_text.moveCaretToTop(false);
				addAndMakeVisible(m_text);
				setSize(Width, Height);
			}
			void resized() override { m_text.setBounds(-6, 0, getWidth() + 12, getHeight() - g1gui::CardView::CloseH - 10); }

		private:
			static constexpr int Width = 760, Height = 380;
			juce::TextEditor m_text;
		};
	}

	std::unique_ptr<juce::Component> Editor::createSettings()
	{
		return std::make_unique<InstanceView>(juce::String(m_processor.describe()));
	}
}
