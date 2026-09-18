add_library(g1Lib STATIC g1mc.cpp g1mc.h g1flash.h)
target_link_libraries(g1Lib PUBLIC 68kEmu)
target_include_directories(g1Lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/.. ${GEARMULATOR_DIR}/source)
