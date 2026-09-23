cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 23)

include(warnings)

set(TARGET ev2_test)
project(${TARGET})

file(GLOB SOURCES "*.cpp" "*.c")

add_executable(${TARGET} ${SOURCES})

target_link_libraries(${TARGET} PRIVATE
	samples-shared
	engine
)

ev2_add_mount(${TARGET} NAME pde DIR resource)

