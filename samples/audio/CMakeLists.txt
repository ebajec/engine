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

# --- Apple ---

if(APPLE)
	enable_language(OBJCXX)

	target_sources(${TARGET} PRIVATE 
        apple/capture_apple.mm
    )

	set_target_properties(${TARGET} PROPERTIES
		OBJCXX_STANDARD 23
		OBJCXX_STANDARD_REQUIRED ON
	)

	# set_source_files_properties(apple/capture_apple.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)

	find_library(COREAUDIO_FRAMEWORK CoreAudio REQUIRED)
	find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)

    target_link_libraries(${TARGET} PRIVATE 
        ${COREAUDIO_FRAMEWORK} 
        ${FOUNDATION_FRAMEWORK}
    )

	# This target is a command-line executable, so embed its privacy usage text.
	set(AUDIO_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/apple/Info.plist")
	target_link_options(${TARGET} PRIVATE "LINKER:-sectcreate,__TEXT,__info_plist,${AUDIO_INFO_PLIST}")
	set_property(TARGET ${TARGET} APPEND PROPERTY LINK_DEPENDS "${AUDIO_INFO_PLIST}")

	# LaunchServices gives the bundle its own privacy identity. A terminal may
	# otherwise inherit the request and lack NSAudioCaptureUsageDescription.
	find_program(CODESIGN_EXECUTABLE codesign REQUIRED)
	set(AUDIO_APP_BUNDLE "$<TARGET_FILE_DIR:${TARGET}>/${TARGET}.app")
	add_custom_command(TARGET ${TARGET} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${AUDIO_APP_BUNDLE}/Contents/MacOS"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${TARGET}>" "${AUDIO_APP_BUNDLE}/Contents/MacOS/${TARGET}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different "${AUDIO_INFO_PLIST}" "${AUDIO_APP_BUNDLE}/Contents/Info.plist"
		COMMAND "${CODESIGN_EXECUTABLE}" --force --sign - "${AUDIO_APP_BUNDLE}"
		VERBATIM
	)
endif()
