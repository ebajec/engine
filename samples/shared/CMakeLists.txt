cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)
include(warnings)

file(GLOB SOURCES "src/*.cpp" "src/*.c")

add_library(samples-shared STATIC ${SOURCES})

target_include_directories(samples-shared PUBLIC include)

target_link_libraries(samples-shared PUBLIC
	ev2-engine
	ev2-editor
	glfw
    glad
    imgui
	stb
    OpenGL::GL
    glm::glm)
