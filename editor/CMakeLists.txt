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
	ev2-engine
	glfw
	imgui
	glm::glm
)

target_compile_definitions(ev2-editor PRIVATE EV2_ENABLE_IMGUI)

enable_warnings(ev2-editor)
