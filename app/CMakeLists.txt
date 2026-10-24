# EmuHost: the running G1 with flash, ALSA MIDI, audio and real time. Used by the console
# (g1run) and the window (g1gui).
# Audio and MIDI have two backends. On Linux the native one (ALSA sequencer ports, JACK graph
# with the back panel's port names) is worth keeping, so it is the default there. Everywhere else
# there is no choice: JUCE, which carries CoreAudio, WASAPI, ASIO and CoreMIDI. The JUCE one can
# be forced on Linux too (-DG1_BACKEND=juce), which is how it gets tested without a Mac.
if(UNIX AND NOT APPLE)
	set(G1_BACKEND "native" CACHE STRING "Audio and MIDI backend: native (ALSA/JACK) or juce")
else()
	set(G1_BACKEND "juce" CACHE STRING "Audio and MIDI backend: native (ALSA/JACK) or juce")
endif()
set_property(CACHE G1_BACKEND PROPERTY STRINGS native juce)

if(G1_BACKEND STREQUAL "juce" AND NOT G1_HAVE_JUCE)
	message(FATAL_ERROR "G1_BACKEND=juce needs JUCE: set G1_JUCE_DIR (see the top-level CMakeLists)")
endif()
message(STATUS "g1Host: ${G1_BACKEND} audio and MIDI backend")

# g1Core: the engine and what every front end shares (ROM search, settings file, statistics).
# No devices and no threads of its own: the plugin builds on it without the backends below.
add_library(g1Core STATIC engine.cpp engine.h hostconfig.cpp hostconfig.h hostlog.cpp hostlog.h diaglog.cpp diaglog.h romfinder.cpp romfinder.h
	slotkeeper.cpp slotkeeper.h synthsettings.cpp synthsettings.h presetslink.cpp presetslink.h audiobridge.h directlink.cpp directlink.h
	midirouting.cpp midirouting.h)
target_include_directories(g1Core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# So a clone works with no setup: the repo's own Roms/ is one of the folders searched.
target_compile_definitions(g1Core PRIVATE G1_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(g1Core PUBLIC g1Lib)
if(WIN32)
	target_link_libraries(g1Core PUBLIC ws2_32)
endif()

# Shared audio queues and conversion, without a ROM or audio device.
find_package(Threads REQUIRED)
add_executable(g1audiobridgetest audiobridgetest.cpp)
target_link_libraries(g1audiobridgetest PRIVATE Threads::Threads)
add_test(NAME g1audiobridgetest COMMAND g1audiobridgetest)
set_tests_properties(g1audiobridgetest PROPERTIES TIMEOUT 30 LABELS g1)

# Parameter values as the editor reads them (g1Lib/g1format.h). No ROM needed.
add_executable(g1formattest formattest.cpp)
target_link_libraries(g1formattest PRIVATE g1Lib)
add_test(NAME g1formattest COMMAND g1formattest)

# The SlotKeeper against an emulated G1 (issue #25). Needs a ROM, so without one it reports itself
# skipped.
add_executable(g1slotkeepertest slotkeepertest.cpp)
target_link_libraries(g1slotkeepertest PRIVATE g1Core)
add_test(NAME g1slotkeepertest COMMAND g1slotkeepertest)
set_tests_properties(g1slotkeepertest PROPERTIES TIMEOUT 300 LABELS g1 SKIP_RETURN_CODE 77)

# The synth's banks for the Presets page: the list decoded, a bank read and a patch loaded.
add_executable(g1presetstest presetstest.cpp)
target_link_libraries(g1presetstest PRIVATE g1Core)
add_test(NAME g1presetstest COMMAND g1presetstest)
set_tests_properties(g1presetstest PROPERTIES TIMEOUT 300 LABELS g1 SKIP_RETURN_CODE 77)

# The synth settings read and written through the PC Port, as an editor does.
add_executable(g1synthsettingstest synthsettingstest.cpp)
target_link_libraries(g1synthsettingstest PRIVATE g1Core)
add_test(NAME g1synthsettingstest COMMAND g1synthsettingstest)
set_tests_properties(g1synthsettingstest PROPERTIES TIMEOUT 300 LABELS g1 SKIP_RETURN_CODE 77)

# An editor's upload with long waits between packets, with and without the settings' link kept out (#81).
add_executable(g1uploadgaptest uploadgaptest.cpp)
target_link_libraries(g1uploadgaptest PRIVATE g1Core)
add_test(NAME g1uploadgaptest COMMAND g1uploadgaptest)
set_tests_properties(g1uploadgaptest PROPERTIES TIMEOUT 600 LABELS g1 SKIP_RETURN_CODE 77)

# A kept flash comes back without being formatted again (the OS's marks survive loadFlash).
add_executable(g1flashkeeptest flashkeeptest.cpp)
target_link_libraries(g1flashkeeptest PRIVATE g1Core)
add_test(NAME g1flashkeeptest COMMAND g1flashkeeptest)
set_tests_properties(g1flashkeeptest PROPERTIES TIMEOUT 300 LABELS g1 SKIP_RETURN_CODE 77)

# The direct link for Animatek NME (issue #8), on its own: no ROM, no emulator.
add_executable(g1directlinktest directlinktest.cpp)
target_link_libraries(g1directlinktest PRIVATE g1Core)
add_test(NAME g1directlinktest COMMAND g1directlinktest)
set_tests_properties(g1directlinktest PROPERTIES TIMEOUT 60 LABELS g1)

add_library(g1Host STATIC emuhost.cpp emuhost.h miditransport.cpp miditransport.h)
target_link_libraries(g1Host PUBLIC g1Core)

if(G1_BACKEND STREQUAL "juce")
	target_sources(g1Host PRIVATE juceaudio.h jucemidi.h)
	target_compile_definitions(g1Host PUBLIC G1_BACKEND_JUCE=1
		JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
	target_link_libraries(g1Host PUBLIC juce::juce_audio_devices juce::juce_recommended_config_flags)
	# The console front end has no window, so it needs JUCE's message loop started by hand.
	target_compile_definitions(g1Host PUBLIC JUCE_STANDALONE_APPLICATION=1)
	if(WIN32)
		target_compile_definitions(g1Host PUBLIC JUCE_ASIO=1)
		set(G1_WINDOWS_MIDI_SDK "" CACHE PATH "Experimental Windows MIDI Services SDK package (0.99.81)")
		set(G1_CPPWINRT "" CACHE FILEPATH "C++/WinRT 3 cppwinrt.exe")
		if(G1_WINDOWS_MIDI_SDK)
			if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_SYSTEM_PROCESSOR MATCHES "[Aa][Rr][Mm]" OR CMAKE_GENERATOR_PLATFORM MATCHES "[Aa][Rr][Mm]")
				message(FATAL_ERROR "The experimental Windows MIDI SDK integration currently supports x64 only")
			endif()
			set(G1_HAVE_WINDOWS_MIDI TRUE)
			message(WARNING "Windows MIDI SDK preview: local development only; do not redistribute this runtime")
			if(NOT EXISTS "${G1_CPPWINRT}")
				message(FATAL_ERROR "G1_WINDOWS_MIDI_SDK requires G1_CPPWINRT")
			endif()
			execute_process(COMMAND "${G1_CPPWINRT}" -input sdk
				-input "${G1_WINDOWS_MIDI_SDK}/ref/native/Windows.Devices.Midi2.winmd"
				-output "${CMAKE_BINARY_DIR}/midi-projection" COMMAND_ERROR_IS_FATAL ANY)
			target_sources(g1Host PRIVATE windowsmidi.cpp windowsmidi.h)
			target_include_directories(g1Host PRIVATE "${CMAKE_BINARY_DIR}/midi-projection")
			target_compile_definitions(g1Host PUBLIC G1_WINDOWS_MIDI=1)
			target_link_libraries(g1Host PUBLIC runtimeobject)
			function(g1_copy_midi_runtime target)
				add_custom_command(TARGET ${target} POST_BUILD
					COMMAND ${CMAKE_COMMAND} -E copy_if_different
					"${G1_WINDOWS_MIDI_SDK}/runtimes/win-x64/native/Windows.Devices.Midi2.dll"
					"${G1_WINDOWS_MIDI_SDK}/runtimes/win-x64/native/Windows.Devices.Midi2.pri"
					"$<TARGET_FILE_DIR:${target}>")
			endfunction()
		endif()
	endif()
else()
	target_sources(g1Host PRIVATE alsamidi.h alsaaudio.h jackaudio.h)
	target_compile_definitions(g1Host PUBLIC G1_BACKEND_NATIVE=1)
	target_link_libraries(g1Host PUBLIC asound)
endif()

# JACK (with PipeWire, pipewire-jack): four outputs and two inputs, like the back panel.
# Only for the native backend; JUCE brings its own.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
	pkg_check_modules(JACK QUIET jack)
endif()
if(JACK_FOUND AND G1_BACKEND STREQUAL "native")
	target_compile_definitions(g1Host PUBLIC G1_HAVE_JACK=1)
	target_include_directories(g1Host PUBLIC ${JACK_INCLUDE_DIRS})
	target_link_libraries(g1Host PUBLIC ${JACK_LIBRARIES})
else()
	message(STATUS "g1run: no JACK, ALSA only")
endif()

add_executable(g1run g1run.cpp)
target_link_libraries(g1run PRIVATE g1Host)
if(G1_HAVE_WINDOWS_MIDI)
	g1_copy_midi_runtime(g1run)
	add_executable(g1midicheck ../tools/g1midicheck.cpp)
	target_link_libraries(g1midicheck PRIVATE g1Host)
	g1_copy_midi_runtime(g1midicheck)
endif()

if(G1_HAVE_JUCE)
	# The build's version for the Report issue button: the tag and commit when Git knows them.
	execute_process(COMMAND git describe --tags --always --dirty
		WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" OUTPUT_VARIABLE g1_build_version
		OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
	if(NOT g1_build_version)
		set(g1_build_version "unknown")
	endif()
	# The panel's look (app/gui/skin), built into the window and the plugin: no files to ship.
	juce_add_binary_data(g1Skin NAMESPACE G1Skin HEADER_NAME G1Skin.h SOURCES
		gui/skin/background.png gui/skin/knob.png gui/skin/dial.png gui/skin/dial_indent.png gui/skin/dial_shadow.png
		gui/skin/button_wide.png gui/skin/button_wide_held.png gui/skin/button_grey.png gui/skin/button_tall.png gui/skin/button_tilted.png gui/skin/button_tilted_held.png gui/skin/led.png gui/skin/small_lcd.png gui/skin/small_lcd_dark.png gui/skin/knob_shadow.png gui/skin/settings_panel.png gui/skin/presets_panel.png gui/skin/master_tune_bg.png gui/skin/octave_shift.png)
	# Animatek NME's code that makes a .pch into the G1's upload (app/nme, its README), with its
	# module descriptions built in: compiled into the window and the plugin, which have JUCE.
	juce_add_binary_data(g1NmeData NAMESPACE G1NmeData HEADER_NAME G1NmeData.h SOURCES nme/modules.xml)
	set(G1_NME_SOURCES
		${CMAKE_CURRENT_SOURCE_DIR}/nme/model/ModuleDescriptions.cpp ${CMAKE_CURRENT_SOURCE_DIR}/nme/model/ModuleTags.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/nme/model/Patch.cpp ${CMAKE_CURRENT_SOURCE_DIR}/nme/model/PatchSerializer.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/nme/model/PchFileIO.cpp ${CMAKE_CURRENT_SOURCE_DIR}/nme/midi/UploadPacketizer.cpp
		${CMAKE_CURRENT_SOURCE_DIR}/gui/PchUpload.cpp ${CMAKE_CURRENT_SOURCE_DIR}/gui/PchUpload.h)
	set(G1_NME_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/nme)
	# Load .pch against an emulated G1: a patch made into packets, uploaded and stored in a bank.
	juce_add_console_app(g1pchtest PRODUCT_NAME "g1pchtest")
	target_sources(g1pchtest PRIVATE pchtest.cpp ${G1_NME_SOURCES})
	target_include_directories(g1pchtest PRIVATE ${G1_NME_INCLUDE} ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_definitions(g1pchtest PRIVATE G1_SOURCE_DIR="${CMAKE_SOURCE_DIR}" JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
	target_link_libraries(g1pchtest PRIVATE g1Core g1NmeData juce::juce_core juce::juce_events juce::juce_graphics juce::juce_data_structures)
	add_test(NAME g1pchtest COMMAND g1pchtest)
	set_tests_properties(g1pchtest PROPERTIES TIMEOUT 300 LABELS g1 SKIP_RETURN_CODE 77)
	add_subdirectory(gui)
	add_subdirectory(plugin)
endif()
