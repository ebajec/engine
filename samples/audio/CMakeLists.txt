cmake_minimum_required(VERSION 3.27)
set(CMAKE_CXX_STANDARD 23)

include(warnings)

set(TARGET audio_viz)
project(${TARGET})

# Single precision fftw. vcpkg and CMake built installs provide a config
# package; distro packages (fftw-devel, libfftw3-dev, brew fftw) provide
# pkg-config files.
find_package(FFTW3f CONFIG QUIET)
if(FFTW3f_FOUND)
	set(FFTW3F_TARGET FFTW3::fftw3f)
else()
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(FFTW3F REQUIRED IMPORTED_TARGET fftw3f)
	set(FFTW3F_TARGET PkgConfig::FFTW3F)
endif()

file(GLOB SOURCES "*.cpp" "*.c")

add_executable(${TARGET} ${SOURCES})

target_link_libraries(${TARGET} PRIVATE
	samples-shared
	ev2-engine
	miniaudio
	${FFTW3F_TARGET}
)

ev2_add_mount(${TARGET} NAME audio DIR resource)
