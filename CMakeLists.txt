cmake_minimum_required(VERSION 3.10)
project(csv)

if(CSV_CXX_STANDARD)
	set(CMAKE_CXX_STANDARD ${CSV_CXX_STANDARD})
else()
	set(CMAKE_CXX_STANDARD 17)
endif(CSV_CXX_STANDARD)

option(BUILD_PYTHON "Build Python Binding" OFF)
option(CSV_BUILD_SINGLE_INCLUDE_TEST "Build single-header smoke test (requires Python)" OFF)
option(ENABLE_CODE_COVERAGE "Enable code coverage instrumentation" OFF)
option(CSV_ENABLE_THREADS "Enable multi-threaded CSV parsing" ON)

if(EMSCRIPTEN AND CSV_ENABLE_THREADS)
  message(STATUS "Emscripten target detected: forcing CSV_ENABLE_THREADS=OFF")
  set(CSV_ENABLE_THREADS OFF CACHE BOOL "Enable multi-threaded CSV parsing" FORCE)
endif()

if(CSV_ENABLE_THREADS)
  message(STATUS "CSV_ENABLE_THREADS enabled: multi-threaded mode")
else()
  message(STATUS "CSV_ENABLE_THREADS disabled: single-threaded mode")
endif()

message("Building CSV library using C++${CMAKE_CXX_STANDARD}")

if(CSV_ENABLE_THREADS)
  set(THREADS_PREFER_PTHREAD_FLAG TRUE)
  find_package(Threads QUIET)
  if(NOT Threads_FOUND)
    message(STATUS "Threads package not found: forcing CSV_ENABLE_THREADS=OFF")
    set(CSV_ENABLE_THREADS OFF CACHE BOOL "Enable multi-threaded CSV parsing" FORCE)
  endif()
endif()

if(MSVC)
	# Make Visual Studio report accurate C++ version
	# See: https://devblogs.microsoft.com/cppblog/msvc-now-correctly-reports-__cplusplus/
  # /Wall emits warnings about the C++ standard library
	# /permissive- enables standards conformance (disables MSVC extensions)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /GS- /Zc:__cplusplus /W4 /permissive-")
else()
	# Ignore Visual Studio pragma regions
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas")
	
	if(ENABLE_CODE_COVERAGE)
		message("Code coverage instrumentation enabled")
		# -fprofile-update=atomic prevents negative counter corruption when
		# background reader threads write to the same .gcda file concurrently.
		set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} --coverage -Og -fprofile-update=atomic")
	endif()
endif(MSVC)

set(CSV_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(CSV_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CSV_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include/)
set(CSV_SOURCE_DIR ${CSV_INCLUDE_DIR}/internal/)
set(CSV_TEST_DIR ${CMAKE_CURRENT_LIST_DIR}/tests)

include_directories(${CSV_INCLUDE_DIR})

## Load developer specific CMake settings
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    SET(CSV_DEVELOPER TRUE)
endif()

## Main Library
add_subdirectory(${CSV_SOURCE_DIR})

# build the python binding for the library 
if (${BUILD_PYTHON})
    message("Building Python bindings for the library.")
    add_subdirectory(python)
endif()

## Executables
option(CSV_BUILD_PROGRAMS "Allow to disable building of programs" ON)
if (CSV_BUILD_PROGRAMS)
    add_subdirectory("programs")
endif()

## Developer settings
if (CSV_DEVELOPER)
    # Allow for performance profiling
    if (MSVC)
	    target_link_options(csv PUBLIC /PROFILE)
        # Treat warnings as errors
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /WX")
    endif()
     
    # More error messages, treat warnings as errors.
    if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} \
        -Wall -Wextra -Wpedantic -Wsign-compare \
        -Wwrite-strings -Wpointer-arith -Winit-self \
        -Wconversion -Wno-sign-conversion -Werror")
    endif()

    # Generate a single header library
    if(CMAKE_VERSION VERSION_LESS "3.12")
      find_package(PythonInterp 3 QUIET)
    else()
      find_package(Python3 COMPONENTS Interpreter)
    endif()
    if(Python3_Interpreter_FOUND OR PYTHONINTERP_FOUND)
      if(Python3_Interpreter_FOUND)
        set(CSV_PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
      else()
        set(CSV_PYTHON_EXECUTABLE ${PYTHON_EXECUTABLE})
      endif()

      set(CSV_SINGLE_INCLUDE_GENERATED_DIR ${CSV_BUILD_DIR}/single_include_generated)
      set(CSV_SINGLE_INCLUDE_GENERATED_HEADER ${CSV_SINGLE_INCLUDE_GENERATED_DIR}/csv.hpp)
          file(GLOB_RECURSE CSV_SINGLE_HEADER_INPUTS CONFIGURE_DEPENDS
            ${CSV_INCLUDE_DIR}/*.hpp
            ${CSV_INCLUDE_DIR}/*.h
            ${CSV_INCLUDE_DIR}/*.cpp
          )

      add_custom_command(
          OUTPUT ${CSV_SINGLE_INCLUDE_GENERATED_HEADER}
          COMMAND ${CMAKE_COMMAND} -E make_directory ${CSV_SINGLE_INCLUDE_GENERATED_DIR}
          COMMAND ${CSV_PYTHON_EXECUTABLE} single_header.py ${CSV_SINGLE_INCLUDE_GENERATED_HEADER}
          WORKING_DIRECTORY ${CSV_ROOT_DIR}
            DEPENDS ${CSV_ROOT_DIR}/single_header.py ${CSV_SINGLE_HEADER_INPUTS}
          COMMENT "Generating single-header csv.hpp"
      )

      add_custom_target(generate_single_header
          DEPENDS ${CSV_SINGLE_INCLUDE_GENERATED_HEADER}
      )

      # Single header compilation test (optional, mainly for validation)
      if(CSV_BUILD_SINGLE_INCLUDE_TEST)
          # Keep this target out of the default "all" build so CI can run it
          # explicitly in its dedicated smoke-test step.
          add_subdirectory(single_include_test EXCLUDE_FROM_ALL)
      endif()
    else()
      message(WARNING "Python3 not found, skipping target 'generate_single_header'.")
      if(CSV_BUILD_SINGLE_INCLUDE_TEST)
        message(FATAL_ERROR "CSV_BUILD_SINGLE_INCLUDE_TEST=ON requires a Python 3 interpreter for single-header generation.")
      endif()
    endif()

    # Documentation
    find_package(Doxygen QUIET)
    if(DOXYGEN_FOUND)
      add_custom_target(doxygen
          COMMAND ${DOXYGEN_EXECUTABLE} ${CSV_ROOT_DIR}/Doxyfile
          WORKING_DIRECTORY ${CSV_ROOT_DIR}
      )
    else()
      message(WARNING "Doxygen not found, skipping target 'doxygen'.")
    endif()

    ## Tests
    enable_testing()
    add_subdirectory("tests")

    # Code coverage
    #find_program( GCOV_PATH gcov )
    #if(GCOV_PATH)
    #    set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/modules")
    #    include(CodeCoverage)
    #    append_coverage_compiler_flags()
    #    set(ENV{CSV_TEST_ROOT} ${CSV_TEST_DIR})
    #    setup_target_for_coverage_gcovr_html(
    #      NAME csv_coverage
    #      EXECUTABLE csv_test
    #      EXCLUDE "tests/*"
    #    )
    #endif()
endif()
