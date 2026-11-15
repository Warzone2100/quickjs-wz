##################
# quickjs-wz tests
#
# Only the qjs library is linked (no qjs / quickjs-libc), so these build with
# every compiler WZ supports, including MSVC and Emscripten.

enable_language(CXX)

set(QJS_UPSTREAM_TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs/tests")

# Upstream test scripts that only need the core engine (plus the small host
# shim provided by run-test-script.c)
set(QJS_UPSTREAM_TEST_SCRIPTS
	test_language
	test_builtin
	test_bigint
	test_closure
	test_loop
)

function(qjs_wz_test_target_setup _target)
	target_link_libraries(${_target} PRIVATE qjs)
	if(MSVC)
		# QuickJS' default JS stack limit is 1 MiB, the same as the default
		# Windows main thread stack - match WZ's /STACK setting
		set_property(TARGET ${_target} APPEND_STRING PROPERTY LINK_FLAGS " /STACK:8000000")
	elseif(EMSCRIPTEN)
		# NODERAWFS: let the test runner read the test scripts from the host filesystem under node
		set_property(TARGET ${_target} APPEND_STRING PROPERTY LINK_FLAGS " -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=10485760")
	endif()
endfunction()

# Script runner + upstream engine tests
add_executable(qjs-wz-run-test-script run-test-script.c)
qjs_wz_test_target_setup(qjs-wz-run-test-script)
foreach(_script IN LISTS QJS_UPSTREAM_TEST_SCRIPTS)
	add_test(NAME upstream.${_script}
		COMMAND qjs-wz-run-test-script "${QJS_UPSTREAM_TESTS_DIR}/${_script}.js"
		WORKING_DIRECTORY "${QJS_UPSTREAM_TESTS_DIR}")
endforeach()

# WZ extensions (limited context, global lookup, random state, debugger, FreeRuntime2)
add_executable(qjs-wz-test-extensions test-wz-extensions.c)
qjs_wz_test_target_setup(qjs-wz-test-extensions)
add_test(NAME wz.extensions COMMAND qjs-wz-test-extensions)

# C++ consumer test: WZ includes quickjs.h from C++ (and builds with warnings
# as errors), so the public headers must compile cleanly as C++ with WZ's
# warning flags. These mirror CONFIGURE_WZ_COMPILER_WARNINGS in warzone2100's
# CMakeLists.txt, so this test fails if (and only if) a header change would
# break WZ's own build.
set(QJS_WZ_CXX_WARNING_FLAGS)
if(MSVC)
	list(APPEND QJS_WZ_CXX_WARNING_FLAGS
		/W4
		/w34204 /w34512 /we4005 /w34191 /w34263 /w34264 /w34265 /w34266
		/w14905 /w14906 /w14928 /w44289 /w14836 /w14946
		/WX
		/wd4800 /wd4127
		/wd4100 /wd4244 /wd4702 /wd4245 /wd4706 /wd4018 /wd4389
	)
else()
	include(CheckCXXCompilerFlag)
	set(_wz_cxx_flags
		-Wpedantic -Wall -Wextra -fno-common -Wcast-align -Wcast-qual -Wctor-dtor-privacy
		-Winit-self -Woverloaded-virtual -Wstrict-null-sentinel -Wwrite-strings -Wpointer-arith
		-Wmissing-noreturn -Wnon-virtual-dtor -Wnull-dereference -Wduplicated-cond -Walloc-zero
		-Walloca-larger-than=1024 -Wrestrict -Wnewline-eof -Wrange-loop-analysis -Wcomma
		-Wfloat-conversion -Wformat-security -Wstringop-truncation -Wlogical-op -Wuninitialized
	)
	if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
		# WZ only enables -Wshadow for Clang
		list(APPEND _wz_cxx_flags -Wshadow -Wno-shadow-field-in-constructor)
	endif()
	list(APPEND _wz_cxx_flags
		-Werror
		-Wno-error=float-conversion -Wno-error=unused-but-set-variable -Wno-error=psabi
		-Wno-sign-compare -Wno-unused-parameter -Wno-format-truncation -Wno-array-bounds
	)
	foreach(_flag IN LISTS _wz_cxx_flags)
		string(MAKE_C_IDENTIFIER "QJS_WZ_HAVE_CXX_FLAG${_flag}" _have_flag)
		check_cxx_compiler_flag("${_flag}" ${_have_flag})
		if(${_have_flag})
			list(APPEND QJS_WZ_CXX_WARNING_FLAGS "${_flag}")
		endif()
	endforeach()
endif()

set(_cxx_standards 17)
if(NOT CMAKE_VERSION VERSION_LESS 3.12)
	list(APPEND _cxx_standards 20)
endif()
foreach(_std IN LISTS _cxx_standards)
	set(_target qjs-wz-test-cxx${_std})
	add_executable(${_target} test-cxx-consumer.cpp)
	qjs_wz_test_target_setup(${_target})
	set_target_properties(${_target} PROPERTIES CXX_STANDARD ${_std} CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
	target_compile_options(${_target} PRIVATE ${QJS_WZ_CXX_WARNING_FLAGS})
	add_test(NAME wz.cxx${_std}-consumer COMMAND ${_target})
endforeach()
