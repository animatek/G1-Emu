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

add_library(g1Host STATIC emuhost.cpp emuhost.h romfinder.cpp romfinder.h audiobridge.h)
target_include_directories(g1Host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# So a clone works with no setup: the repo's own Roms/ is one of the folders searched.
target_compile_definitions(g1Host PRIVATE G1_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(g1Host PUBLIC g1Lib)

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
	add_subdirectory(gui)
endif()
