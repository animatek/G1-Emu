# g1gui: the window with the G1 panel.
juce_add_gui_app(g1gui PRODUCT_NAME "G1-Emu")
target_sources(g1gui PRIVATE Main.cpp Panel.cpp Panel.h Settings.cpp Settings.h LcdFont.h)
target_compile_definitions(g1gui PRIVATE
	G1_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
	JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_APPLICATION_NAME_STRING="G1-Emu" JUCE_APPLICATION_VERSION_STRING="0.1")
target_link_libraries(g1gui PRIVATE g1Host juce::juce_gui_basics juce::juce_data_structures juce::juce_recommended_config_flags)
