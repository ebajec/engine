cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

set(PROJECT_NAME "engine")
project(${PROJECT_NAME})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(OpenGL_GL_PREFERENCE GLVND)  

set(RESOURCE_PATH "${CMAKE_SOURCE_DIR}/res")

if(RESOURCE_PATH)
	add_definitions(-DRESOURCE_PATH="${RESOURCE_PATH}")
	message(STATUS "Default resource path : ${RESOURCE_PATH}") 
endif()

find_package(OpenGL REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(yaml-cpp REQUIRED)
find_package(glfw3 REQUIRED)

add_compile_options(-g)

option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
if(ENABLE_ASAN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
message(STATUS "AddressSanitizer Enabled")
	add_compile_options(
    -fsanitize=address
    -fno-omit-frame-pointer    
    -g                          
  )
	add_link_options(
    -fsanitize=address
  )
endif()

option(ENABLE_TSAN "Enable ThreadSanitizer" OFF)
if(ENABLE_TSAN AND (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU"))
	message(STATUS "ThreadSanitizer Enabled")
	add_compile_options(
    -fsanitize=thread
    -fno-omit-frame-pointer    
    -g                          
  )
	add_link_options(
    -fsanitize=thread
  )
endif()


set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -mavx2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")

add_subdirectory(deps)
#add_subdirectory(src)
add_subdirectory(engine)
add_subdirectory(samples)

