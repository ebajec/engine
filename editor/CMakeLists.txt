cmake_minimum_required(VERSION 3.27)

include(warnings)

add_library(ev2-editor STATIC
	src/editor.cpp
	src/viewport.cpp
	src/image_viewer.cpp

	src/motion_camera.cpp
	src/panning_camera.cpp
)

target_include_directories(ev2-editor PUBLIC include)

target_link_libraries(ev2-editor PUBLIC
	engine
	glfw
	imgui
	glm::glm
)

# Only editor.cpp reads this; samples get the GUI through this library.
target_compile_definitions(ev2-editor PRIVATE ENABLE_IMGUI)

enable_warnings(ev2-editor)
