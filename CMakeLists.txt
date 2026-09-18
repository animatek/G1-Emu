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

add_subdirectory(${GEARMULATOR_DIR}/source gearmulator EXCLUDE_FROM_ALL)

add_subdirectory(g1Lib)
add_subdirectory(tools)
