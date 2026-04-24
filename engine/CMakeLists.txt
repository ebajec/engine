cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

include(clang-warnings)

set(TARGET "engine")
project(${TARGET})

file(GLOB CORE_SOURCES 
	"src/*.cpp" 
	"src/*.c" 
	"src/debug/*.cpp" 
	"src/utils/*.cpp" 
	"src/globe/*.cpp" 
)

add_library(${TARGET})

target_compile_definitions(engine PUBLIC -DRESOURCE_PATH="${RESOURCE_PATH}")

target_include_directories(${TARGET} PUBLIC include)
target_include_directories(${TARGET} PRIVATE 
	src 
	include/engine
)

target_sources(${TARGET} PRIVATE ${CORE_SOURCES})

target_link_libraries(${TARGET} PUBLIC 
    imgui
	spirv-reflect-static
	stb
	yaml-cpp::yaml-cpp
    glm::glm
)

#add_subdirectory(src/backends/opengl)
add_subdirectory(src/backends/vulkan)

# Sanitizers

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
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
elseif(ENABLE_ASAN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
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

target_compile_options(${TARGET} PUBLIC -g)

enable_clang_warnings(${TARGET})
