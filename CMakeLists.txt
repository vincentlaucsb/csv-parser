cmake_minimum_required(VERSION 3.15)
project(csv)

# enable MSVC_RUNTIME_LIBRARY target property
# see https://cmake.org/cmake/help/latest/policy/CMP0091.html
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

if(DEFINED CSV_CXX_STANDARD AND NOT "${CSV_CXX_STANDARD}" STREQUAL "")
	set(CMAKE_CXX_STANDARD ${CSV_CXX_STANDARD})
elseif(NOT DEFINED CMAKE_CXX_STANDARD OR "${CMAKE_CXX_STANDARD}" STREQUAL "")
	set(CMAKE_CXX_STANDARD 20)
endif()

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(BUILD_PYTHON "Build Python Binding" OFF)
option(CSV_BUILD_SINGLE_INCLUDE_TEST "Build single-header smoke test (requires single_header)" OFF)
option(ENABLE_CODE_COVERAGE "Enable code coverage instrumentation" OFF)
option(CSV_ENABLE_THREADS "Enable multi-threaded CSV parsing" ON)
option(CSV_NO_SIMD "Disable SIMD fast paths at compile time" OFF)
option(CSV_FORCE_AVX2 "Force AVX2 compiler flags for this build" OFF)

string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" CSV_SYSTEM_PROCESSOR_LOWER)
set(CSV_TARGET_X86 OFF)
if(CSV_SYSTEM_PROCESSOR_LOWER MATCHES "^(x86_64|amd64|x64|i[3-6]86)$")
  set(CSV_TARGET_X86 ON)
endif()

if(DEFINED ENV{CSV_FETCHCONTENT_BASE_DIR} AND NOT DEFINED FETCHCONTENT_BASE_DIR)
  set(FETCHCONTENT_BASE_DIR "$ENV{CSV_FETCHCONTENT_BASE_DIR}" CACHE PATH "Base directory for FetchContent dependencies")
endif()

if(EMSCRIPTEN AND CSV_ENABLE_THREADS)
  message(STATUS "Emscripten target detected: forcing CSV_ENABLE_THREADS=OFF")
  set(CSV_ENABLE_THREADS OFF CACHE BOOL "Enable multi-threaded CSV parsing" FORCE)
endif()

if(EMSCRIPTEN)
  message(STATUS "Emscripten target detected: enabling C++ exceptions (-fexceptions)")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fexceptions")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fexceptions")
endif()

if(CSV_ENABLE_THREADS)
  message(STATUS "CSV_ENABLE_THREADS enabled: multi-threaded mode")
else()
  message(STATUS "CSV_ENABLE_THREADS disabled: single-threaded mode")
endif()

message("Building CSV library using C++${CMAKE_CXX_STANDARD}")

if(ENABLE_CODE_COVERAGE)
	message("Code coverage instrumentation enabled")
	add_compile_definitions(CSV_CODE_COVERAGE=1)
endif()

if(BUILD_PYTHON)
  # fastpycsv links the static csv target into a Python extension module, so Linux
  # builds need the csv objects compiled as position-independent code.
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

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
  # /Wall emits warnings about the C++ standard library
	# /permissive- enables standards conformance (disables MSVC extensions)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /GS- /W4 /permissive-")
else()
	# Ignore Visual Studio pragma regions
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas")
	
	if(ENABLE_CODE_COVERAGE)
		# -fprofile-update=atomic prevents negative counter corruption when
		# background reader threads write to the same .gcda file concurrently.
		set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} --coverage -O0 -fprofile-update=atomic")
	endif()
endif(MSVC)

# SIMD policy:
# - CSV_NO_SIMD=ON: hard-disable SIMD in code
# - default: enable AVX2 codegen on MSVC; GCC/Clang use CI matrix or manual flags
if(CSV_NO_SIMD)
  message(STATUS "CSV SIMD: disabled (CSV_NO_SIMD=ON)")
else()
  if(CSV_FORCE_AVX2 AND NOT CSV_TARGET_X86)
    message(FATAL_ERROR "CSV_FORCE_AVX2 requires an x86/x64 target; detected '${CMAKE_SYSTEM_PROCESSOR}'")
  elseif(MSVC AND CSV_TARGET_X86)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /arch:AVX2")
    message(STATUS "CSV SIMD: AVX2 enabled (MSVC /arch:AVX2 added)")
  elseif(CSV_FORCE_AVX2)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx2")
    message(STATUS "CSV SIMD: AVX2 codegen requested (-mavx2 added)")
  elseif(NOT CSV_TARGET_X86)
    message(STATUS "CSV SIMD: x86 AVX2 flags unavailable for ${CMAKE_SYSTEM_PROCESSOR}; using compiler default ISA")
  else()
    message(STATUS "CSV SIMD: default config (CI/user supplies ISA flags if needed)")
  endif()
endif()

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

set(FASTPYCSV_BOOTSTRAP_BINARY_DIR "${CSV_BUILD_DIR}/fastpycsv" CACHE PATH
    "Build directory used by the fastpycsv bootstrap target when BUILD_PYTHON is OFF")
set(FASTPYCSV_BOOTSTRAP_PYTHON_EXECUTABLE "" CACHE FILEPATH
    "Python executable used by the fastpycsv bootstrap target")
if(FASTPYCSV_BOOTSTRAP_PYTHON_EXECUTABLE AND NOT EXISTS "${FASTPYCSV_BOOTSTRAP_PYTHON_EXECUTABLE}")
    message(STATUS "Ignoring missing FASTPYCSV_BOOTSTRAP_PYTHON_EXECUTABLE=${FASTPYCSV_BOOTSTRAP_PYTHON_EXECUTABLE}")
    set(FASTPYCSV_BOOTSTRAP_PYTHON_EXECUTABLE "" CACHE FILEPATH
        "Python executable used by the fastpycsv bootstrap target" FORCE)
endif()

# Build the Python binding for the library. The real `fastpycsv` module target is
# created when BUILD_PYTHON is enabled. Otherwise, keep a bootstrap target so
# existing CMake builds can build fastpycsv without reconfiguring the main tree.
if (${BUILD_PYTHON})
    message("Building Python bindings for the library.")
    enable_testing()
    add_subdirectory(python)
else()
    set(_CSV_FASTPYCSV_BOOTSTRAP_CONFIG_ARGS
        -S ${CSV_ROOT_DIR}
        -B ${FASTPYCSV_BOOTSTRAP_BINARY_DIR}
        -DBUILD_PYTHON=ON
        -DCSV_BUILD_TESTS=OFF
        -DCSV_BUILD_PROGRAMS=OFF
        -DCSV_ENABLE_THREADS=${CSV_ENABLE_THREADS}
        -DCSV_NO_SIMD=${CSV_NO_SIMD}
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5
        -U Python_EXECUTABLE
        -U PYTHON_EXECUTABLE
    )
    if(CMAKE_GENERATOR)
        list(APPEND _CSV_FASTPYCSV_BOOTSTRAP_CONFIG_ARGS -G ${CMAKE_GENERATOR})
    endif()
    if(CMAKE_GENERATOR_PLATFORM)
        list(APPEND _CSV_FASTPYCSV_BOOTSTRAP_CONFIG_ARGS -A ${CMAKE_GENERATOR_PLATFORM})
    endif()
    if(CMAKE_GENERATOR_TOOLSET)
        list(APPEND _CSV_FASTPYCSV_BOOTSTRAP_CONFIG_ARGS -T ${CMAKE_GENERATOR_TOOLSET})
    endif()
    if(CMAKE_BUILD_TYPE)
        list(APPEND _CSV_FASTPYCSV_BOOTSTRAP_CONFIG_ARGS -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE})
    endif()
    if(FASTPYCSV_BOOTSTRAP_PYTHON_EXECUTABLE)
        list(APPEND _CSV_FASTPYCSV_BOOTSTRAP_CONFIG_ARGS
            -DPython_EXECUTABLE=${FASTPYCSV_BOOTSTRAP_PYTHON_EXECUTABLE}
            -DPYTHON_EXECUTABLE=${FASTPYCSV_BOOTSTRAP_PYTHON_EXECUTABLE}
        )
    endif()

    add_custom_target(fastpycsv
        COMMAND ${CMAKE_COMMAND} ${_CSV_FASTPYCSV_BOOTSTRAP_CONFIG_ARGS}
        COMMAND ${CMAKE_COMMAND} -E env --unset=PATH "Path=$ENV{Path}" ${CMAKE_COMMAND} --build ${FASTPYCSV_BOOTSTRAP_BINARY_DIR} --target fastpycsv --config $<CONFIG>
        USES_TERMINAL
        COMMENT "Configuring and building fastpycsv in ${FASTPYCSV_BOOTSTRAP_BINARY_DIR}"
    )
    unset(_CSV_FASTPYCSV_BOOTSTRAP_CONFIG_ARGS)
endif()

## Executables
option(CSV_BUILD_PROGRAMS "Allow to disable building of programs" ON)
if (CSV_BUILD_PROGRAMS)
    add_subdirectory("programs")
endif()

## Tests
option(CSV_BUILD_TESTS "Allow to disable building of tests" ON)

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
    set(CSV_SINGLE_HEADER_EXECUTABLE "" CACHE FILEPATH "Path to the single_header executable")
    if(CSV_SINGLE_HEADER_EXECUTABLE AND NOT EXISTS "${CSV_SINGLE_HEADER_EXECUTABLE}")
      message(STATUS "Ignoring missing CSV_SINGLE_HEADER_EXECUTABLE=${CSV_SINGLE_HEADER_EXECUTABLE}")
      set(CSV_SINGLE_HEADER_EXECUTABLE "" CACHE FILEPATH "Path to the single_header executable" FORCE)
    endif()
    if(NOT CSV_SINGLE_HEADER_EXECUTABLE)
      find_program(_CSV_SINGLE_HEADER_EXECUTABLE_FOUND
          NAMES single_header single_header.exe
          HINTS ${CSV_ROOT_DIR}
      )
      if(_CSV_SINGLE_HEADER_EXECUTABLE_FOUND)
        set(CSV_SINGLE_HEADER_EXECUTABLE "${_CSV_SINGLE_HEADER_EXECUTABLE_FOUND}" CACHE FILEPATH "Path to the single_header executable" FORCE)
      endif()
      unset(_CSV_SINGLE_HEADER_EXECUTABLE_FOUND CACHE)
    endif()

    if(CSV_SINGLE_HEADER_EXECUTABLE)
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
          COMMAND ${CSV_SINGLE_HEADER_EXECUTABLE}
            --config ${CSV_ROOT_DIR}/single_header.json
            --output ${CSV_SINGLE_INCLUDE_GENERATED_HEADER}
          WORKING_DIRECTORY ${CSV_ROOT_DIR}
            DEPENDS ${CSV_ROOT_DIR}/single_header.json ${CSV_SINGLE_HEADER_INPUTS}
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
      message(WARNING "single_header executable not found, skipping target 'generate_single_header'.")
      if(CSV_BUILD_SINGLE_INCLUDE_TEST)
        message(FATAL_ERROR "CSV_BUILD_SINGLE_INCLUDE_TEST=ON requires the single_header executable.")
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
    if (CSV_BUILD_TESTS)
      enable_testing()
      add_subdirectory("tests")
    endif()
endif()
