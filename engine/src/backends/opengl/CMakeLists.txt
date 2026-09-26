cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

include(warnings)

file(GLOB SOURCES
	"./*.cpp"
)

target_sources(ev2-engine PRIVATE 
	${SOURCES}
)
target_link_libraries(ev2-engine PUBLIC 
	glad
	OpenGL::GL
)
target_compile_definitions(ev2-engine PUBLIC 
	EV2_BACKEND_OPENGL
)
