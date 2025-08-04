cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 23)

include(clang-warnings)

set(TARGET globe)
project(${TARGET})

add_executable(${TARGET} main.cpp globe.cpp tile_cache.cpp)

target_link_libraries(${TARGET} PRIVATE
	samples
	engine
)
