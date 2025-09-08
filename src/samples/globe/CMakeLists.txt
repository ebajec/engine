cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 23)

include(clang-warnings)

set(TARGET boxes)
project(${TARGET})

file(GLOB SOURCES "*.cpp" "*.c")

add_executable(${TARGET} ${SOURCES})

target_link_libraries(${TARGET} PRIVATE
	samples
	engine
)
