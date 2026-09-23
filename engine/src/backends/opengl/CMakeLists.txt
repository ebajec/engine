cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

include(warnings)

file(GLOB SOURCES
	"./*.cpp"
)

target_sources(engine PRIVATE 
	${SOURCES}
)
target_link_libraries(engine PUBLIC 
	glad
	OpenGL::GL
)
target_compile_definitions(engine PUBLIC 
	EV2_BACKEND_OPENGL
)
