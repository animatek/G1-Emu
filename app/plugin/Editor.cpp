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
	bool Editor::lastController(int& _channel, int& _cc, uint32_t& _serial) { return m_processor.lastController(_channel, _cc, _serial); }
	bool Editor::selectedControllers() const { return m_processor.selectedControllers(); }
	void Editor::setSelectedControllers(const bool _on) { m_processor.setSelectedControllers(_on); }
	bool Editor::octaveShift() const { return m_processor.octaveShift(); }
	void Editor::setOctaveShift(const bool _on) { m_processor.setOctaveShift(_on); }
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
		// What Settings shows in the plugin, on the panel's card: the output gain (issue #65: the
		// standalone's slider, missing here until now, was the only way to make up for a quiet
		// patch), then this instance (its ROM, latency, where its patches came from, its PC Port and
		// direct link) and the notice, as the About card shows its text.
		class InstanceView : public juce::Component
		{
		public:
			InstanceView(juce::RangedAudioParameter& _gain, const juce::String& _instance, const bool _expanded, std::function<void(bool)> _setExpanded)
				: m_gainAttachment(_gain, m_gain)
			{
				// Regular or Expanded (8 DSPs, about twice the voices): a new G1, so the change
				// restarts it, keeping the project's patches as a restart does.
				label(m_modelLabel, "Model");
				m_model.addItem("Regular  (4 DSPs)", 1);
				m_model.addItem("Expanded  (8 DSPs, twice the voices)", 2);
				m_model.setSelectedId(_expanded ? 2 : 1, juce::dontSendNotification);
				m_model.setTooltip("The G1 with or without the expansion board. Changing it restarts the G1; "
					"the project's patches and settings stay, what is in the slots and not stored is kept with the project");
				m_model.onChange = [this, set = std::move(_setExpanded)] { set(m_model.getSelectedId() == 2); };
				addAndMakeVisible(m_model);

				label(m_gainLabel, "Output level");
				m_gain.setSliderStyle(juce::Slider::LinearHorizontal);
				m_gain.setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 22);
				m_gain.setTextValueSuffix(" dB");
				m_gain.setTooltip("The OS caps the G1's master volume at -36 dB; +36 dB undoes it, and a new "
					"instance starts at +48 dB so patches sit level with other instruments. Lower it for a loud patch.");
				addAndMakeVisible(m_gain);

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
			void resized() override
			{
				m_modelLabel.setBounds(0, 0, 130, GainRowH);
				m_model.setBounds(136, 0, 320, GainRowH);
				constexpr int gainY = GainRowH + 8, textY = gainY + GainRowH + 6;
				m_gainLabel.setBounds(0, gainY, 130, GainRowH);
				m_gain.setBounds(136, gainY, getWidth() - 136, GainRowH);
				m_text.setBounds(-6, textY, getWidth() + 12, getHeight() - textY - g1gui::CardView::CloseH - 10);
			}

		private:
			static void label(juce::Label& _l, const juce::String& _text)
			{
				_l.setText(_text, juce::dontSendNotification);
				_l.setColour(juce::Label::textColourId, juce::Colours::white);
				_l.setJustificationType(juce::Justification::centredRight);
			}
			static constexpr int Width = 760, Height = 412, GainRowH = 24;
			juce::Label m_modelLabel, m_gainLabel;
			juce::ComboBox m_model;
			juce::Slider m_gain;
			juce::SliderParameterAttachment m_gainAttachment;
			juce::TextEditor m_text;
		};
	}

	std::unique_ptr<juce::Component> Editor::createSettings()
	{
		// The restart replaces this panel, so it comes later, on the message thread (as restart()).
		return std::make_unique<InstanceView>(m_processor.gainParameter(), juce::String(m_processor.describe()), m_processor.expanded(),
			[e = juce::Component::SafePointer<Editor>(this)](const bool _on)
			{
				juce::MessageManager::callAsync([e, _on] { if(e) e->m_processor.setExpanded(_on); });
			});
	}
}
