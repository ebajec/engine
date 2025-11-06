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

# Windows things
if(WIN32)
  include(FetchContent)

  FetchContent_Declare(
    glm
    GIT_REPOSITORY	https://github.com/g-truc/glm.git
    # GIT_TAG bf71a834948186f4097caa076cd2663c69a10e1e #refs/tags/1.0.1
  )

  FetchContent_MakeAvailable(glm)

  FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    # GIT_TAG f7320141120f720aecc4c32be25586e7da9eb978 # Can be a tag (yaml-cpp-x.x.x), a commit hash, or a branch name (master)
  )

  FetchContent_MakeAvailable(yaml-cpp)

  FetchContent_Declare(
    glfw3
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    # GIT_TAG 3.4
  )

  # Avoid building extras
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(glfw3) 
else()
  find_package(glm CONFIG REQUIRED)
  find_package(yaml-cpp REQUIRED)
  find_package(glfw3 REQUIRED)
endif()

find_package(OpenGL REQUIRED)

add_compile_options(-g)

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS}   -mavx2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")

add_subdirectory(deps)
add_subdirectory(engine)
add_subdirectory(samples)

