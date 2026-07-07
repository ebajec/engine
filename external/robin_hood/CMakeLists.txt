cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

include(clang-warnings)

project(robin_hood)
add_library(robin_hood INTERFACE)
target_include_directories(robin_hood INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
