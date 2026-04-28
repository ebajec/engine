cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

include(clang-warnings)

project(vulkan_memory_allocator)
add_library(vulkan_memory_allocator STATIC vk_mem_alloc.cpp)
target_include_directories(vulkan_memory_allocator PUBLIC include)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR
    CMAKE_C_COMPILER_ID  MATCHES "Clang")
	target_compile_options(vulkan_memory_allocator PUBLIC
		-Wno-nullability-extension
		-Wno-nullability-completeness
	)
endif()


