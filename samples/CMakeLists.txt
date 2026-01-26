cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

include(clang-warnings)

project(samples)

file(GLOB SOURCES "shared/*.cpp" "shared/*.c" "src/*/*.cpp" "src/*/*.c")

add_library(samples-common STATIC ${SOURCES})
target_include_directories(samples-common PUBLIC shared )

target_link_libraries(samples-common PUBLIC 
	engine
	glfw
    glad
    imgui
	stb
    OpenGL::GL
    glm::glm)

add_subdirectory(ev2_test)
add_subdirectory(ev2_globe)
