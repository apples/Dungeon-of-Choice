cmake_minimum_required (VERSION 3.0)
project(LD34)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=core2 -mtune=bdver4")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=core2 -mtune=bdver4")

add_subdirectory(ginseng)
add_subdirectory(raspberry)
add_subdirectory(sushi)
add_subdirectory(jsoncpp)
add_subdirectory(soloud)

set_property(TARGET soloud APPEND PROPERTY COMPILE_DEFINITIONS DISABLE_SIMD)

add_executable(game src/main.cpp src/util.hpp)
set_property(TARGET game PROPERTY CXX_STANDARD 14)
target_link_libraries(game ginseng raspberry sushi jsoncpp_lib_static soloud Winmm)
