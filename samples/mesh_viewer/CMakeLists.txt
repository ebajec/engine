cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 23)

include(warnings)

project(mesh_viewer)

add_executable(mesh_viewer 
	main.cpp 
)

target_link_libraries(mesh_viewer PRIVATE
	samples-shared
	ev2-engine
	ev2-editor
)
