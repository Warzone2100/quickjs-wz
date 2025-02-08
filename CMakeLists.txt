cmake_minimum_required(VERSION 3.10...3.24)
project (quickjs-wz C)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
	cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

##################
# NOTES:
# This is a WZ-specific QuickJS CMake build config

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

##################
# QuickJS library

add_subdirectory(quickjs-ng)

target_compile_definitions(qjs PRIVATE
	QJS_DISABLE_ATOMICS
)

target_include_directories(qjs PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/quickjs-wz-extensions>
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
	# GCC 9.3.0+ has been tested to be fine at -O2
	if((CMAKE_C_COMPILER_VERSION VERSION_LESS 9.3) AND (CMAKE_C_COMPILER_VERSION VERSION_GREATER 7.0))
		# Avoid SEGFAULT with earlier GCC and fcode-hoisting
		message(STATUS "QuickJS: Using -fno-code-hoisting (${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION})")
		target_compile_options(qjs PRIVATE -fno-code-hoisting)
	endif()
endif()

# Alias the quickjs-ng "qjs" library name to the previous library name that WZ used
add_library(quickjs ALIAS qjs)

include(CheckCompilerFlagsOutput)
if(MSVC)
	# Disable some warnings for QuickJS
	set(QUICKJS_COMPILE_OPTIONS "")

	# warning C4146: unary minus operator applied to unsigned type, result still unsigned
	set(QUICKJS_COMPILE_OPTIONS ${QUICKJS_COMPILE_OPTIONS} /wd4146)

	# warning C4456: declaration of 'var' hides previous local declaration
	set(QUICKJS_COMPILE_OPTIONS ${QUICKJS_COMPILE_OPTIONS} /wd4456)

	# warning C4457: declaration of 'var' hides function parameter
	set(QUICKJS_COMPILE_OPTIONS ${QUICKJS_COMPILE_OPTIONS} /wd4457)

	# warning C4464: relative include path contains '..'
	# (caused by the WZ extension includes at the end of quickjs.c)
	set(QUICKJS_COMPILE_OPTIONS ${QUICKJS_COMPILE_OPTIONS} /wd4464)

	# warning C4702: unreachable code
	set(QUICKJS_COMPILE_OPTIONS ${QUICKJS_COMPILE_OPTIONS} /wd4702)

	# warning C4334: 'operator' : result of 32-bit shift implicitly converted to 64 bits (was 64-bit shift intended?)
	set(QUICKJS_COMPILE_OPTIONS ${QUICKJS_COMPILE_OPTIONS} /wd4334)

	target_compile_options(qjs PRIVATE ${QUICKJS_COMPILE_OPTIONS})
else()
	# Disable some warnings from QuickJS

	set(_supported_quickjs_c_compiler_flags "")

	# -Wcast-align				(GCC 3.4+, Clang 3.2+)
	check_compiler_flags_output("-Werror -Wno-cast-align -Wno-error=cpp" COMPILER_TYPE C   OUTPUT_FLAGS "-Wno-cast-align" OUTPUT_VARIABLE _supported_quickjs_c_compiler_flags APPEND)

	# -Wshadow					(GCC 3.4+, Clang 3.2+)
	check_compiler_flags_output("-Werror -Wno-shadow -Wno-error=cpp" COMPILER_TYPE C   OUTPUT_FLAGS "-Wno-shadow" OUTPUT_VARIABLE _supported_quickjs_c_compiler_flags APPEND)

	# -Wimplicit-int-float-conversion			(Clang)
	check_compiler_flags_output("-Werror -Wno-implicit-int-float-conversion -Wno-error=cpp" COMPILER_TYPE C   OUTPUT_FLAGS "-Wno-implicit-int-float-conversion" OUTPUT_VARIABLE _supported_quickjs_c_compiler_flags APPEND)

	# -Wimplicit-const-int-float-conversion		(Clang)
	check_compiler_flags_output("-Werror -Wno-implicit-const-int-float-conversion -Wno-error=cpp" COMPILER_TYPE C   OUTPUT_FLAGS "-Wno-implicit-const-int-float-conversion" OUTPUT_VARIABLE _supported_quickjs_c_compiler_flags APPEND)

	# -Wunused-variable
	check_compiler_flags_output("-Werror -Wno-unused-variable -Wno-error=cpp" COMPILER_TYPE C   OUTPUT_FLAGS "-Wno-unused-variable" OUTPUT_VARIABLE _supported_quickjs_c_compiler_flags APPEND)

	# -Wunused-but-set-variable
	check_compiler_flags_output("-Werror -Wno-unused-but-set-variable -Wno-error=cpp" COMPILER_TYPE C   OUTPUT_FLAGS "-Wno-unused-but-set-variable" OUTPUT_VARIABLE _supported_quickjs_c_compiler_flags APPEND)

	# -Wconditional-uninitialized
	check_compiler_flags_output("-Werror -Wno-conditional-uninitialized -Wno-error=cpp" COMPILER_TYPE C   OUTPUT_FLAGS "-Wno-conditional-uninitialized" OUTPUT_VARIABLE _supported_quickjs_c_compiler_flags APPEND)

	# -Wcomma
	check_compiler_flags_output("-Werror -Wno-comma -Wno-error=cpp" COMPILER_TYPE C   OUTPUT_FLAGS "-Wno-comma" OUTPUT_VARIABLE _supported_quickjs_c_compiler_flags APPEND)

	# -Wassign-enum
	check_compiler_flags_output("-Werror -Wno-assign-enum -Wno-error=cpp" COMPILER_TYPE C   OUTPUT_FLAGS "-Wno-assign-enum" OUTPUT_VARIABLE _supported_quickjs_c_compiler_flags APPEND)

	# -Wmacro-redefined (for Mingw builds)
	check_compiler_flags_output("-Werror -Wno-macro-redefined -Wno-error=cpp" COMPILER_TYPE C   OUTPUT_FLAGS "-Wno-macro-redefined" OUTPUT_VARIABLE _supported_quickjs_c_compiler_flags APPEND)

	if (NOT _supported_quickjs_c_compiler_flags STREQUAL "")
		string(REPLACE " " ";" _supported_quickjs_c_compiler_flags "${_supported_quickjs_c_compiler_flags}")
		target_compile_options(qjs PRIVATE ${_supported_quickjs_c_compiler_flags})
	endif()
endif()

