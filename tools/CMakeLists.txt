add_executable(g1boot g1boot.cpp)
target_link_libraries(g1boot PRIVATE g1Lib)

add_executable(dspdis dspdis.cpp)
target_link_libraries(dspdis PRIVATE dsp56kEmu)
