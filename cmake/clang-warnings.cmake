function(enable_clang_warnings target)
  	# Only apply when using Clang
  	if (CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR
      	CMAKE_C_COMPILER_ID  MATCHES "Clang")
    	target_compile_options(${target} PRIVATE
			# Base warnings
			-Wall                  # all the important warnings
			-Wextra                # extra, useful warnings
			-Wpedantic             # ISO-compliance
			# Conversion and sign
			-Wconversion           # detect implicit conversions
			-Wsign-conversion      # detect sign-related conversions
			# Code correctness
			-Wshadow               # shadowed variables
			-Wnull-dereference     # null-pointer derefs
			-Wdouble-promotion     # implicit float→double
			-Wformat=2             # printf/scanf format checking
			-Wcast-align           # cast increases alignment requirement
			-Wmissing-prototypes   # use of functions without declarations
			-Wmissing-declarations # non-static functions must be declared
			# Unused / unreachable
			-Wunused-variable      # unused local variables
			-Wunreachable-code     # code that can never be reached
			# Control flow
			-Wimplicit-fallthrough # switch-case fall-through
			-Wswitch-enum          # switches over enums should handle all values
			# Thread safety (Clang only)
			-Wthread-safety

			-Wno-unused-parameter
		   	-Wno-format-nonliteral
			-Wno-gnu-zero-variadic-macro-arguments
    	)
  	endif()
endfunction()
