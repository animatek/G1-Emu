add_executable(g1boot g1boot.cpp)
target_link_libraries(g1boot PRIVATE g1Lib)

add_executable(dspdis dspdis.cpp)
target_link_libraries(dspdis PRIVATE dsp56kEmu)

add_executable(g1dspcheck g1dspcheck.cpp)
target_link_libraries(g1dspcheck PRIVATE dsp56kEmu)
add_test(NAME g1dspcheck COMMAND g1dspcheck)
# Labelled so CI can run our tests and not the ones Gearmulator registers from its own tree,
# whose binaries are EXCLUDE_FROM_ALL here and so are never built: ctest -L g1.
set_tests_properties(g1dspcheck PROPERTIES TIMEOUT 30 LABELS g1)
add_subdirectory(patchtest)
