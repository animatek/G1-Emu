add_executable(g1run g1run.cpp alsamidi.h)
target_link_libraries(g1run PRIVATE g1Lib asound)
