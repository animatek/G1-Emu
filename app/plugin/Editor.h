#pragma once

// The plugin's window: the same panel as g1gui (app/gui/Panel.h), or, while there is no G1 to
// show, why not: no ROM (with the way to give it one), or the host has not started the audio yet.

#include "Panel.h"
#include "engine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>

namespace g1plugin
{
	class Processor;

	class Editor : public juce::AudioProcessorEditor, private g1gui::PanelHost, private juce::Timer
	{
	public:
		explicit Editor(Processor& _processor);
		~Editor() override;

		// The processor is about to replace the engine: the panel must not outlive it.
		void engineGoing();

		void paint(juce::Graphics& _g) override;
		void resized() override;
		void childBoundsChanged(juce::Component* _child) override;

	private:
		void timerCallback() override;
		void rebuild();
		void chooseRom();

		// PanelHost
		g1::Microcontroller& mc() override { return m_engine->mc(); }
		g1app::HostStats stats() override;
		bool extrasOpen() const override;
		void setExtrasOpen(bool _open) override;
		bool knobDisplays() const override;
		void setKnobDisplays(bool _on) override;
		bool knobFollowsPatch() const override;
		void setKnobFollowsPatch(bool _on) override;
		uint32_t randomExcluded() const override;
		void setRandomExcluded(uint32_t _knobs) override;
		float panelScale() const override;
		void setPanelScale(float _scale) override;
		void fitTo(double _aspect);
		juce::String settingsTooltip() const override { return "ROM, latency, and where this instance's patches came from"; }
		std::unique_ptr<juce::Component> createSettings() override;
		g1app::SynthSettingsLink& synthSettings() override;
		g1app::PresetsLink& presets() override;
		bool canRestart() const override { return true; }
		void restart() override;
		juce::String restartNote() const override
		{
			return "As switching it off and on, then loading this project again: the banks, the slots and "
				"the knobs come back as they are now. The PC Port stays open.";
		}

		Processor& m_processor;
		g1app::Engine* m_engine = nullptr;
		int m_generation = -1;
		std::unique_ptr<g1gui::PanelView> m_panel;	// the panel, at the size the user gave the window

		// Shown instead of the panel while there is no G1.
		juce::Label m_message;
		juce::TextButton m_openFolder{"Open the ROM folder"}, m_pickRom{"Choose a ROM file..."};
		std::unique_ptr<juce::FileChooser> m_chooser;
	};
}
