cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 20)

project(miniaudio)

find_package(Threads REQUIRED)

add_library(miniaudio STATIC miniaudio.c)

target_include_directories(miniaudio PUBLIC miniaudio)

# Backends (PulseAudio, ALSA, ...) are loaded with dlopen at runtime, so no
# audio dev packages are needed at build time.
target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
	target_link_libraries(miniaudio PUBLIC m)
endif()
