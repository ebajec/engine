cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 23)

include(clang-warnings)

project(default)

add_executable(default main.cpp)

target_link_libraries(default PRIVATE
	samples
	engine
)
