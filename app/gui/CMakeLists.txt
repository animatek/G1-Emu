# g1gui: the window with the G1 panel.
juce_add_gui_app(g1gui PRODUCT_NAME "G1-Emu" ICON_BIG ${CMAKE_CURRENT_SOURCE_DIR}/icon.png)	# Mike Fiction's, the standalone's only (not the plugin's)
target_sources(g1gui PRIVATE Main.cpp Panel.cpp Panel.h SynthSettingsView.cpp SynthSettingsView.h Overlay.cpp Overlay.h Settings.cpp Settings.h MicroPanel.cpp MicroPanel.h LcdFont.h)
target_compile_definitions(g1gui PRIVATE
	G1_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
	G1_BUILD_VERSION="${g1_build_version}"
	JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="G1-Emu" JUCE_APPLICATION_VERSION_STRING="0.1")
target_sources(g1gui PRIVATE ${G1_NME_SOURCES})
target_include_directories(g1gui PRIVATE ${G1_NME_INCLUDE})
target_link_libraries(g1gui PRIVATE g1Host g1Skin g1NmeData juce::juce_gui_basics juce::juce_data_structures juce::juce_recommended_config_flags)
if(G1_HAVE_WINDOWS_MIDI)
	g1_copy_midi_runtime(g1gui)
endif()
