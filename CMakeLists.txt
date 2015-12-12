cmake_minimum_required (VERSION 3.0)
project(LD34)

add_subdirectory(ginseng)
add_subdirectory(raspberry)
add_subdirectory(sushi)
add_subdirectory(jsoncpp)
add_subdirectory(soloud)

add_executable(game src/main.cpp)
set_property(TARGET game PROPERTY CXX_STANDARD 14)
target_link_libraries(game ginseng raspberry sushi jsoncpp_lib_static soloud)
