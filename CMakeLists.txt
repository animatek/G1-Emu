cmake_minimum_required(VERSION 3.22)

# G1-Emu: the Nord Modular G1 emulated on the Gearmulator core (68331 + DSP56303).
# The core is NOT copied: it is linked from a clone of gearmulator-md-mm (GPLv3).
project(g1emu VERSION 0.0.1 LANGUAGES C CXX)

set(GEARMULATOR_DIR "$ENV{HOME}/src/gearmulator-md-mm" CACHE PATH "Clone of joelanders/gearmulator-md-mm")
if(NOT EXISTS "${GEARMULATOR_DIR}/source/mc68k")
	message(FATAL_ERROR "mc68k not found in GEARMULATOR_DIR=${GEARMULATOR_DIR}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(${GEARMULATOR_DIR}/base.cmake)
include(CTest)

# From Gearmulator's tree only the cores are needed (68k, DSP, hardwareLib).
foreach(synth OSIRUS OSTIRUS VAVRA XENIA NODALRED2X JE8086 ELEKTRON)
	set(${CMAKE_PROJECT_NAME}_SYNTH_${synth} OFF CACHE BOOL "" FORCE)
endforeach()
set(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

# Gearmulator's top-level CMake generates synthLib/buildconfig.h in its source tree.
# Only the cores and the MIDI bridge the G1 needs are included.
set(ASMJIT_NO_INSTALL TRUE)
set(BUILD_SHARED_LIBS OFF)
add_subdirectory(${GEARMULATOR_DIR}/source/dsp56300/source gearmulator/dsp56300 EXCLUDE_FROM_ALL)
add_subdirectory(${GEARMULATOR_DIR}/source/mc68k gearmulator/mc68k EXCLUDE_FROM_ALL)
add_library(g1Midi STATIC
	${GEARMULATOR_DIR}/source/hardwareLib/sciMidi.cpp
	${GEARMULATOR_DIR}/source/synthLib/midiBufferParser.cpp)
target_include_directories(g1Midi PUBLIC ${GEARMULATOR_DIR}/source)
target_link_libraries(g1Midi PUBLIC 68kEmu)
include(cmake/Dsp56300.cmake)

# JUCE, for the window (app/gui) and the test bench (tools/patchtest). By default the one in
# Animatek NME next to this repo (../Nomad2026/JUCE); G1_JUCE_DIR to use another. Without JUCE
# only the console and the tools are built.
set(G1_JUCE_DIR "${CMAKE_SOURCE_DIR}/../Nomad2026/JUCE" CACHE PATH "JUCE tree")
set(G1_HAVE_JUCE OFF)
if(EXISTS "${G1_JUCE_DIR}/CMakeLists.txt")
	add_subdirectory(${G1_JUCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
	set(G1_HAVE_JUCE ON)
else()
	message(STATUS "No JUCE in ${G1_JUCE_DIR}: no window and no test bench")
endif()

enable_testing()
add_subdirectory(g1Lib)
add_subdirectory(tools)
add_subdirectory(app)
