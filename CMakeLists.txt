cmake_minimum_required(VERSION 3.9)
project(csv)

if(CSV_CXX_STANDARD)
	set(CMAKE_CXX_STANDARD ${CSV_CXX_STANDARD})
else()
	set(CMAKE_CXX_STANDARD 17)
endif(CSV_CXX_STANDARD)

message("Building CSV library using C++${CMAKE_CXX_STANDARD}")

# Defines CSV_HAS_CXX17 in compatibility.hpp
if (CMAKE_VERSION VERSION_LESS "3.12.0")
	add_definitions(-DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD})
else()
	add_compile_definitions(CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD})
endif()

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads QUIET REQUIRED)

if(MSVC)
	# Make Visual Studio report accurate C++ version
	# See: https://devblogs.microsoft.com/cppblog/msvc-now-correctly-reports-__cplusplus/
  # /Wall emits warnings about the C++ standard library
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /Zc:__cplusplus /W4")
else()
	# Ignore Visual Studio pragma regions
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og")
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

## Executables
add_subdirectory("programs")

## Developer settings
if (CSV_DEVELOPER)
    # Generate a single header library
    find_package(Python3 QUIET)
    if(Python3_Interpreter_FOUND)
      add_custom_target(generate_single_header
          COMMAND ${Python3_EXECUTABLE} single_header.py > single_include/csv.hpp
          COMMAND ${Python3_EXECUTABLE} single_header.py > single_include_test/csv.hpp
          WORKING_DIRECTORY ${CSV_ROOT_DIR}
      )
    else()
      message(WARNING "Python3 not found, skipping target 'generate_single_header'.")
    endif()

    # Single header compilation test
    add_subdirectory(single_include_test)

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
endif()
