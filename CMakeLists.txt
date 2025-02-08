cmake_minimum_required(VERSION 3.10...3.24)
project (quickjs-wz C)

if(${CMAKE_VERSION} VERSION_LESS 3.12)
	cmake_policy(VERSION ${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION})
endif()

##################
# QuickJS library

add_subdirectory(quickjs-ng)

target_compile_definitions(qjs PRIVATE
	QJS_DISABLE_ATOMICS
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
