cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

project(glad)

add_library(glad STATIC glad/src/glad.c)

target_include_directories(glad PUBLIC glad/include)
