
cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

include(clang-warnings)

set(TARGET "engine")
project(${TARGET})

add_compile_definitions(-DRESOURCE_PATH="${RESOURCE_PATH}")

file(GLOB SOURCES "src/*.cpp" "src/*.c" "src/*/*.cpp" "src/*/*.c")

add_library(${TARGET} STATIC ${SOURCES})

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
message(STATUS "AddressSanitizer Enabled")
	target_compile_options(${TARGET} PUBLIC
		-fsanitize=address
		-fno-omit-frame-pointer    
		-g                          
	)
	target_link_options(${TARGET} PUBLIC
    -fsanitize=address
  )
endif()

option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if(ENABLE_TSAN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
	message(STATUS "ThreadSanitizer Enabled")
	target_compile_options(${TARGET} PUBLIC
		-fsanitize=thread
		-fno-omit-frame-pointer    
		-g                          
  	)
	target_link_options(${TARGET} PUBLIC
    	-fsanitize=thread
  	)
endif()



target_include_directories(${TARGET} PUBLIC include)
target_include_directories(${TARGET} PRIVATE src include/engine)

target_link_libraries(${TARGET} PUBLIC 
    glad
    imgui
	spirv-reflect-static
	stb
    OpenGL::GL
	yaml-cpp::yaml-cpp
    glm::glm)

enable_clang_warnings(${TARGET})
