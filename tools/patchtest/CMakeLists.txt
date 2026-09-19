# g1patchtest: headless test bench. It loads a .pch with NME's own code (the one the editor uses
# to upload it), uploads it to the emulated G1 packet by packet like NME, plays a note and
# measures the output. Only built if NME is next to this repo (../Nomad2026): the emulator does
# not depend on NME.
set(NME_DIR "${CMAKE_SOURCE_DIR}/../Nomad2026" CACHE PATH "Animatek NME tree")
if(NOT G1_HAVE_JUCE OR NOT EXISTS "${NME_DIR}/source/model/PatchSerializer.cpp")
	message(STATUS "g1patchtest: NME not found in ${NME_DIR}; not built")
	return()
endif()

juce_add_console_app(g1patchtest PRODUCT_NAME "g1patchtest")
target_sources(g1patchtest PRIVATE
	g1patchtest.cpp
	${NME_DIR}/source/model/Patch.cpp
	${NME_DIR}/source/model/ModuleDescriptions.cpp
	${NME_DIR}/source/model/ModuleTags.cpp
	${NME_DIR}/source/model/ThemeData.cpp
	${NME_DIR}/source/format/ValueFormatters.cpp
	${NME_DIR}/source/model/PatchSerializer.cpp
	${NME_DIR}/source/model/PatchParser.cpp
	${NME_DIR}/source/model/PchFileIO.cpp
	${NME_DIR}/source/midi/UploadPacketizer.cpp
	${NME_DIR}/source/midi/SysExCodec.cpp
	${NME_DIR}/source/protocol/KnobAssignmentMessage.cpp)
target_include_directories(g1patchtest PRIVATE ${NME_DIR}/source)
target_compile_definitions(g1patchtest PRIVATE
	NME_DATA_DIR="${NME_DIR}/data"
	JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(g1patchtest PRIVATE g1Lib juce::juce_core juce::juce_events juce::juce_data_structures juce::juce_graphics)
