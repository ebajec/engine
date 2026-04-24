cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

project(vulkan_memory_allocator)
add_library(vulkan_memory_allocator STATIC vk_mem_alloc.cpp)
target_include_directories(vulkan_memory_allocator PUBLIC include)

