# EmuHost: el G1 funcionando con flash, MIDI de ALSA, audio y tiempo real. Lo usan la consola
# (g1run) y la ventana (g1gui).
add_library(g1Host STATIC emuhost.cpp emuhost.h alsamidi.h alsaaudio.h jackaudio.h)
target_include_directories(g1Host PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(g1Host PUBLIC g1Lib asound)

# JACK (con PipeWire, pipewire-jack): cuatro salidas y dos entradas como el panel trasero.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
	pkg_check_modules(JACK QUIET jack)
endif()
if(JACK_FOUND)
	target_compile_definitions(g1Host PUBLIC G1_HAVE_JACK=1)
	target_include_directories(g1Host PUBLIC ${JACK_INCLUDE_DIRS})
	target_link_libraries(g1Host PUBLIC ${JACK_LIBRARIES})
else()
	message(STATUS "g1run: sin JACK, solo ALSA")
endif()

add_executable(g1run g1run.cpp)
target_link_libraries(g1run PRIVATE g1Host)

if(G1_HAVE_JUCE)
	add_subdirectory(gui)
endif()
