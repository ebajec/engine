cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

project(spirv-reflect)

file(GLOB SOURCES
    "spirv-reflect/spirv_reflect.c"
    )

add_library(spirv-reflect STATIC ${SOURCES})

target_include_directories(spirv-reflect PUBLIC spirv-reflect)
