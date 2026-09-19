add_executable(g1boot g1boot.cpp)
target_link_libraries(g1boot PRIVATE g1Lib)

add_executable(dspdis dspdis.cpp)
target_link_libraries(dspdis PRIVATE dsp56kEmu)

add_executable(g1dspcheck g1dspcheck.cpp)
target_link_libraries(g1dspcheck PRIVATE dsp56kEmu)
add_test(NAME g1dspcheck COMMAND g1dspcheck)
set_tests_properties(g1dspcheck PROPERTIES TIMEOUT 30)
add_subdirectory(patchtest)
