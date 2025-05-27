cmake_minimum_required(VERSION 3.10...3.31)
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

message(STATUS "QuickJS Library: Begin")

# For now, avoid including the quickjs-ng CMake configuration directly, and explicitly specify our own
# add_subdirectory(quickjs-ng)

set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_C_STANDARD 11)

set(QUICKJS_HEADERS
	quickjs-ng/cutils.h
	quickjs-ng/libregexp.h
	quickjs-ng/libunicode.h
	quickjs-ng/list.h
	quickjs-ng/quickjs.h
	# extensions
	quickjs-wz-extensions/quickjs-debugger.h
	quickjs-wz-extensions/quickjs-limitedcontext.h
)

set(QUICKJS_SOURCES
	quickjs-ng/cutils.c
	quickjs-ng/libregexp.c
	quickjs-ng/libunicode.c
	quickjs-ng/quickjs.c
	quickjs-ng/xsum.c
	# extensions
	# INCLUDED DIRECTLY IN QUICKJS.C: quickjs-wz-extensions/quickjs-debugger.c
	# INCLUDED DIRECTLY IN QUICKJS.C: quickjs-wz-extensions/quickjs-limitedcontext.c
)

list(APPEND qjs_defines _GNU_SOURCE)
list(APPEND qjs_defines QJS_DISABLE_ATOMICS)
if(WIN32)
	list(APPEND qjs_defines WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0601)
endif()

add_library(qjs STATIC ${QUICKJS_HEADERS} ${QUICKJS_SOURCES})
target_compile_definitions(qjs PRIVATE ${qjs_defines})
target_include_directories(qjs PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/quickjs-ng
	${CMAKE_CURRENT_SOURCE_DIR}/quickjs-wz-extensions
)

set(CMAKE_THREAD_PREFER_PTHREAD TRUE)
set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)
target_link_libraries(qjs PRIVATE Threads::Threads)

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

include(CheckCCompilerFlag)

macro(xcheck_add_c_compiler_flag FLAG)
	string(REPLACE "-" "" FLAG_NO_HYPHEN ${FLAG})
	check_c_compiler_flag(${FLAG} QJS_C_COMPILER_SUPPORTS_${FLAG_NO_HYPHEN})
	if(QJS_C_COMPILER_SUPPORTS_${FLAG_NO_HYPHEN})
		target_compile_options(qjs PRIVATE ${FLAG})
	endif()
endmacro()

xcheck_add_c_compiler_flag(-Wall)
xcheck_add_c_compiler_flag(-Wformat=2)
xcheck_add_c_compiler_flag(-funsigned-char)

if(MSVC)
	# Disable some warnings for QuickJS

	xcheck_add_c_compiler_flag(/wd4018) # -Wno-sign-conversion
	xcheck_add_c_compiler_flag(/wd4061) # -Wno-implicit-fallthrough
	xcheck_add_c_compiler_flag(/wd4100) # -Wno-unused-parameter
	xcheck_add_c_compiler_flag(/wd4200) # -Wno-zero-length-array
	xcheck_add_c_compiler_flag(/wd4242) # -Wno-shorten-64-to-32
	xcheck_add_c_compiler_flag(/wd4244) # -Wno-shorten-64-to-32
	xcheck_add_c_compiler_flag(/wd4245) # -Wno-sign-compare
	xcheck_add_c_compiler_flag(/wd4267) # -Wno-shorten-64-to-32
	xcheck_add_c_compiler_flag(/wd4388) # -Wno-sign-compare
	xcheck_add_c_compiler_flag(/wd4389) # -Wno-sign-compare
	xcheck_add_c_compiler_flag(/wd4456) # Hides previous local declaration
	xcheck_add_c_compiler_flag(/wd4457) # Hides function parameter
	xcheck_add_c_compiler_flag(/wd4710) # Function not inlined
	xcheck_add_c_compiler_flag(/wd4711) # Function was inlined
	xcheck_add_c_compiler_flag(/wd4820) # Padding added after construct
	xcheck_add_c_compiler_flag(/wd4996) # -Wdeprecated-declarations
	xcheck_add_c_compiler_flag(/wd5045) # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified

	xcheck_add_c_compiler_flag(/wd4146) # warning C4146: unary minus operator applied to unsigned type, result still unsigned
	xcheck_add_c_compiler_flag(/wd4456) # warning C4456: declaration of 'var' hides previous local declaration
	xcheck_add_c_compiler_flag(/wd4457) # warning C4457: declaration of 'var' hides function parameter

	# warning C4464: relative include path contains '..'
	# (caused by the WZ extension includes at the end of quickjs.c)
	xcheck_add_c_compiler_flag(/wd4464)

	xcheck_add_c_compiler_flag(/wd4702) # warning C4702: unreachable code
	xcheck_add_c_compiler_flag(/wd4334) # warning C4334: 'operator' : result of 32-bit shift implicitly converted to 64 bits (was 64-bit shift intended?)
else()
	# Disable some warnings from QuickJS

	xcheck_add_c_compiler_flag(-Wno-implicit-fallthrough)
	xcheck_add_c_compiler_flag(-Wno-sign-compare)
	xcheck_add_c_compiler_flag(-Wno-missing-field-initializers)
	xcheck_add_c_compiler_flag(-Wno-unused-parameter)
	xcheck_add_c_compiler_flag(-Wno-unused-result)
	xcheck_add_c_compiler_flag(-Wno-stringop-truncation)
	xcheck_add_c_compiler_flag(-Wno-array-bounds)

	xcheck_add_c_compiler_flag(-Wno-cast-align) # -Wcast-align (GCC 3.4+, Clang 3.2+)
	xcheck_add_c_compiler_flag(-Wno-shadow) # -Wshadow (GCC 3.4+, Clang 3.2+)
	xcheck_add_c_compiler_flag(-Wno-implicit-int-float-conversion) # -Wimplicit-int-float-conversion (Clang)
	xcheck_add_c_compiler_flag(-Wno-implicit-const-int-float-conversion) # -Wimplicit-const-int-float-conversion (Clang)
	xcheck_add_c_compiler_flag(-Wno-unused-variable)
	xcheck_add_c_compiler_flag(-Wno-unused-but-set-variable)
	xcheck_add_c_compiler_flag(-Wno-conditional-uninitialized)
	xcheck_add_c_compiler_flag(-Wno-comma)
	xcheck_add_c_compiler_flag(-Wno-assign-enum)
	xcheck_add_c_compiler_flag(-Wno-maybe-uninitialized)

	# -Wmacro-redefined (for Mingw builds) # Only possibly needed if using quickjs-ng CMake configuration directly?
	# xcheck_add_c_compiler_flag(-Wno-macro-redefined)
endif()

message(STATUS "QuickJS Library: End")
