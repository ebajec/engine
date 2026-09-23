cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 23)

include(warnings)

set(TARGET ev2_minimal)
project(${TARGET})

file(GLOB SOURCES "*.cpp" "*.c")

add_executable(${TARGET} ${SOURCES})

target_link_libraries(${TARGET} PRIVATE
	engine
	glfw
)

ev2_add_mount(${TARGET} NAME minimal DIR resource)

