# ev2 build helpers. DEV_CORE_MOUNT and DEV_SHADER_BUILD_MOUNT must be set
# before this is included.

set(EV2_COMPILE_SHADERS_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/../compile_shaders.sh")

# <include root>/<mount> links to each mount root, so shaders include by vfs
# path ("core/shader/frame.glsl"). compile_shaders.sh maintains the same tree
# (next to its output dir); creating it at configure time as well means editor
# tooling (.nvim.lua) can resolve includes before the first build.
set(EV2_SHADER_INCLUDE_ROOT "${CMAKE_BINARY_DIR}/shader_include")

function(_ev2_link_shader_include name dir)
	set(link "${EV2_SHADER_INCLUDE_ROOT}/${name}")
	file(MAKE_DIRECTORY "${EV2_SHADER_INCLUDE_ROOT}")
	file(REMOVE "${link}")
	file(CREATE_LINK "${dir}" "${link}" SYMBOLIC)
endfunction()

_ev2_link_shader_include(core "${DEV_CORE_MOUNT}")

# A single shader build for every registered mount. Every target needs the core
# shaders, so per-target builds would race writing the same outputs. The script
# is incremental, so running it on every build is cheap.
if(NOT TARGET ev2_shaders)
	add_custom_target(ev2_shaders
		COMMAND ${CMAKE_COMMAND} -E env
			"EV2_CORE_MOUNT=${DEV_CORE_MOUNT}"
			"EV2_MOUNTS=$<JOIN:$<TARGET_PROPERTY:ev2_shaders,EV2_MOUNTS>,$<COMMA>>"
			bash "${EV2_COMPILE_SHADERS_SCRIPT}" "${DEV_SHADER_BUILD_MOUNT}"
		COMMENT "Compiling shaders"
		USES_TERMINAL
		VERBATIM
	)
endif()

# ev2_add_mount(<target> NAME <name> DIR <dir>)
#
# Registers <dir> (relative to the calling CMakeLists.txt) as the vfs mount
# <name> for <target>:
#  - <target> is compiled with EV2_PROJECT_MOUNTS, a comma separated list of
#    {"name", "path"} initializers, for the program to mount on startup
#    (see samples/shared/project_mounts.h).
#  - the shaders under <dir> are compiled into the spirv mount before <target>
#    is built.
function(ev2_add_mount target)
	cmake_parse_arguments(PARSE_ARGV 1 ARG "" "NAME;DIR" "")

	if(NOT ARG_NAME OR NOT ARG_DIR)
		message(FATAL_ERROR "ev2_add_mount(${target}): NAME and DIR are required")
	endif()

	cmake_path(ABSOLUTE_PATH ARG_DIR
		BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
		NORMALIZE
		OUTPUT_VARIABLE dir
	)
	string(REGEX REPLACE "/$" "" dir "${dir}")

	if(NOT IS_DIRECTORY "${dir}")
		message(FATAL_ERROR "ev2_add_mount(${target}): ${dir} is not a directory")
	endif()

	set_property(TARGET ${target} APPEND PROPERTY EV2_PROJECT_MOUNTS "{\"${ARG_NAME}\", \"${dir}\"}")
	_ev2_link_shader_include(${ARG_NAME} "${dir}")

	# the definition reads the property at generate time, so later calls for
	# the same target only need to append to it
	get_target_property(mounts_defined ${target} EV2_PROJECT_MOUNTS_DEFINED)
	if(NOT mounts_defined)
		set_property(TARGET ${target} PROPERTY EV2_PROJECT_MOUNTS_DEFINED TRUE)
		target_compile_definitions(${target} PRIVATE
			"EV2_PROJECT_MOUNTS=$<JOIN:$<TARGET_PROPERTY:${target},EV2_PROJECT_MOUNTS>,$<COMMA>>"
		)
		add_dependencies(${target} ev2_shaders)
	endif()

	# several targets can share a mount, but it only needs compiling once
	get_target_property(shader_mounts ev2_shaders EV2_MOUNTS)
	if(NOT "${ARG_NAME}:${dir}" IN_LIST shader_mounts)
		set_property(TARGET ev2_shaders APPEND PROPERTY EV2_MOUNTS "${ARG_NAME}:${dir}")
	endif()
endfunction()
