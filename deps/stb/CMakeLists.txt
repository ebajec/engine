cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

project(stb)

file(GLOB SOURCES
    "stb/src/*.c"
    )

add_library(stb STATIC ${SOURCES})

target_include_directories(stb PUBLIC stb)
