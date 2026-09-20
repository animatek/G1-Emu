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

if(G1_HAVE_JUCE)
	add_subdirectory(gui)
endif()
