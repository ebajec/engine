cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

include(warnings)

project(samples)

add_subdirectory(shared)
add_subdirectory(test)
add_subdirectory(fluid)
add_subdirectory(sorting)
add_subdirectory(minimal)
#add_subdirectory(ev2_globe)
