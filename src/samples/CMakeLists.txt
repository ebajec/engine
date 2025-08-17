cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

include(clang-warnings)

project(samples)

file(GLOB SOURCES "src/*.cpp" "src/*.c" "src/*/*.cpp" "src/*/*.c")

add_library(samples STATIC ${SOURCES})
target_include_directories(samples PUBLIC src )

target_link_libraries(samples PUBLIC 
	engine
	glfw
    glad
    imgui
	stb
    OpenGL::GL
    glm::glm)

add_subdirectory(lines)
add_subdirectory(globe)
