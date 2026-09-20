# EmuHost: the running G1 with flash, ALSA MIDI, audio and real time. Used by the console
# (g1run) and the window (g1gui).
add_library(g1Host STATIC emuhost.cpp emuhost.h romfinder.cpp romfinder.h alsamidi.h alsaaudio.h jackaudio.h)
target_include_directories(g1Host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# So a clone works with no setup: the repo's own Roms/ is one of the folders searched.
target_compile_definitions(g1Host PRIVATE G1_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(g1Host PUBLIC g1Lib asound)

# JACK (with PipeWire, pipewire-jack): four outputs and two inputs, like the back panel.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
	pkg_check_modules(JACK QUIET jack)
endif()
if(JACK_FOUND)
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
