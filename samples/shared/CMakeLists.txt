cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)
include(clang-warnings)

file(GLOB SOURCES "*.cpp" "*/*.cpp" "*.c" "*/*.c")

add_library(samples-shared STATIC ${SOURCES})

target_include_directories(samples-shared PUBLIC .)

target_link_libraries(samples-shared PUBLIC 
	engine
	glfw
    glad
    imgui
	stb
    OpenGL::GL
    glm::glm)
