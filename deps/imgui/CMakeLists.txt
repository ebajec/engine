cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

project(imgui)

file(GLOB SOURCES
    "imgui/*.cpp"
    "imgui/backends/imgui_impl_glfw.cpp"
    "imgui/backends/imgui_impl_opengl3.cpp"
	"implot/implot.cpp"
	"implot/implot_items.cpp"
    )

add_library(imgui STATIC ${SOURCES})

if(WIN32 OR APPLE)
    target_link_libraries(imgui PUBLIC glfw)
endif()

target_include_directories(imgui PUBLIC imgui implot)
