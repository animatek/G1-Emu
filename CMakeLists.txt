cmake_minimum_required(VERSION 3.22)

# G1-Emu: el Nord Modular G1 emulado sobre el nucleo de Gearmulator (68331 + DSP56303).
# El nucleo NO se copia: se enlaza desde un clon de gearmulator-md-mm (GPLv3).
project(g1emu VERSION 0.0.1 LANGUAGES C CXX)

set(GEARMULATOR_DIR "$ENV{HOME}/src/gearmulator-md-mm" CACHE PATH "Clon de joelanders/gearmulator-md-mm")
if(NOT EXISTS "${GEARMULATOR_DIR}/source/mc68k")
	message(FATAL_ERROR "No encuentro mc68k en GEARMULATOR_DIR=${GEARMULATOR_DIR}")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(${GEARMULATOR_DIR}/base.cmake)
include(CTest)

# Del arbol de Gearmulator solo hacen falta los nucleos (68k, DSP, hardwareLib).
foreach(synth OSIRUS OSTIRUS VAVRA XENIA NODALRED2X JE8086 ELEKTRON)
	set(${CMAKE_PROJECT_NAME}_SYNTH_${synth} OFF CACHE BOOL "" FORCE)
endforeach()
set(${CMAKE_PROJECT_NAME}_BUILD_JUCEPLUGIN OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

# El CMake global de Gearmulator genera synthLib/buildconfig.h en su arbol
# fuente. Incluimos solo los nucleos y el puente MIDI que necesita el G1.
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

enable_testing()
add_subdirectory(g1Lib)
add_subdirectory(tools)
add_subdirectory(app)
