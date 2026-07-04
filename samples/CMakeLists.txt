cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

include(clang-warnings)

project(samples)

add_subdirectory(shared)
add_subdirectory(ev2_test)

#add_subdirectory(ev2_fluid)
#add_subdirectory(ev2_globe)
