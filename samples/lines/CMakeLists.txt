cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 23)

include(clang-warnings)

set(TARGET lines)
project(${TARGET})

add_executable(${TARGET} main.cpp)

target_link_libraries(${TARGET} PRIVATE
	samples
	engine
)
