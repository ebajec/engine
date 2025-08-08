cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

include(clang-warnings)

set(TARGET "engine")
project(${TARGET})

add_compile_definitions(-DRESOURCE_PATH="${RESOURCE_PATH}")

file(GLOB SOURCES "./*.cpp" "./*.c" "./*/*.cpp" "./*/*.c")

add_library(${TARGET} STATIC ${SOURCES})

target_include_directories(${TARGET} PUBLIC .)

target_link_libraries(${TARGET} PUBLIC 
    glad
    imgui
	spirv-reflect-static
	stb
    OpenGL::GL
	yaml-cpp
    glm::glm)

enable_clang_warnings(${TARGET})
