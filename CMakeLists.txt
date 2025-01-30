cmake_minimum_required(VERSION 3.9)
project(csv VERSION 2.3.0)

if(CSV_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD ${CSV_CXX_STANDARD})
else()
  set(CMAKE_CXX_STANDARD 17)
endif(CSV_CXX_STANDARD)

option(BUILD_PYTHON "Build Python Binding" OFF)

message("Building CSV library using C++${CMAKE_CXX_STANDARD}")

# Defines CSV_HAS_CXX17 in compatibility.hpp
if(CMAKE_VERSION VERSION_LESS "3.12.0")
  add_definitions(-DCMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD})
else()
  add_compile_definitions(CMAKE_CXX_STANDARD=${CMAKE_CXX_STANDARD})
endif()

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads QUIET REQUIRED)

if(MSVC)
  # Make Visual Studio report accurate C++ version See:
  # https://devblogs.microsoft.com/cppblog/msvc-now-correctly-reports-__cplusplus/
  # /Wall emits warnings about the C++ standard library
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /GS- /Zc:__cplusplus /W4")
else()
  # Ignore Visual Studio pragma regions
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} --coverage -Og")
endif(MSVC)

if(CSV_TEST_INSTALLED_VERSION)
  find_package(${PROJECT_NAME} REQUIRED)
else()
  set(CSV_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})
  set(CSV_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR})
  set(CSV_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include/)
  # set(CSV_SOURCE_DIR ${CSV_INCLUDE_DIR}/internal/)
  set(CSV_TEST_DIR ${CMAKE_CURRENT_LIST_DIR}/tests)

  # Load developer specific CMake settings
  if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(CSV_DEVELOPER TRUE)
  endif()

  # Main Library
  add_library(
    ${PROJECT_NAME} STATIC
    ${CSV_INCLUDE_DIR}/internal/basic_csv_parser.hpp
    ${CSV_INCLUDE_DIR}/internal/basic_csv_parser.cpp
    ${CSV_INCLUDE_DIR}/internal/col_names.cpp
    ${CSV_INCLUDE_DIR}/internal/col_names.hpp
    ${CSV_INCLUDE_DIR}/internal/common.hpp
    ${CSV_INCLUDE_DIR}/internal/csv_format.hpp
    ${CSV_INCLUDE_DIR}/internal/csv_format.cpp
    ${CSV_INCLUDE_DIR}/internal/csv_reader.hpp
    ${CSV_INCLUDE_DIR}/internal/csv_reader.cpp
    ${CSV_INCLUDE_DIR}/internal/csv_reader_iterator.cpp
    ${CSV_INCLUDE_DIR}/internal/csv_row.hpp
    ${CSV_INCLUDE_DIR}/internal/csv_row.cpp
    ${CSV_INCLUDE_DIR}/internal/csv_row_json.cpp
    ${CSV_INCLUDE_DIR}/internal/csv_stat.cpp
    ${CSV_INCLUDE_DIR}/internal/csv_stat.hpp
    ${CSV_INCLUDE_DIR}/internal/csv_utility.cpp
    ${CSV_INCLUDE_DIR}/internal/csv_utility.hpp
    ${CSV_INCLUDE_DIR}/internal/csv_writer.hpp
    ${CSV_INCLUDE_DIR}/internal/data_type.hpp
    ${CSV_INCLUDE_DIR}/external/hedley.h
    ${CSV_INCLUDE_DIR}/external/mio.hpp
    ${CSV_INCLUDE_DIR}/external/string_view.hpp)

  set_target_properties(csv PROPERTIES LINKER_LANGUAGE CXX)
  target_link_libraries(csv PRIVATE Threads::Threads)
  target_include_directories(
    ${PROJECT_NAME} PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
                           $<INSTALL_INTERFACE:include/${PROJECT_NAME}>)

  include(${CMAKE_CURRENT_LIST_DIR}/cmake/getCPM.cmake)

  set(packageProject_VERSION 1.12.0)
  cpmaddpackage(
    NAME
    PackageProject.cmake
    VERSION
    ${packageProject_VERSION}
    URL
    "https://github.com/TheLartians/PackageProject.cmake/archive/refs/tags/v${packageProject_VERSION}.zip"
  )

  string(TOLOWER ${PROJECT_NAME}/version.h VERSION_HEADER_LOCATION)
  string(TOLOWER ${PROJECT_NAME}/export.h EXPORT_HEADER_LOCATION)

  set_property(TARGET ${PROJECT_NAME} PROPERTY VERSION ${PROJECT_VERSION})
  set_property(TARGET ${PROJECT_NAME} PROPERTY SOVERSION ${PROJECT_VERSION})

  packageproject(
    NAME
    ${PROJECT_NAME}
    VERSION
    ${PROJECT_VERSION}
    NAMESPACE
    ${PROJECT_NAME}
    BINARY_DIR
    ${PROJECT_BINARY_DIR}
    INCLUDE_DIR
    ${PROJECT_SOURCE_DIR}/include
    INCLUDE_DESTINATION
    include/${PROJECT_NAME}
    VERSION_HEADER
    "${VERSION_HEADER_LOCATION}"
    EXPORT_HEADER
    "${EXPORT_HEADER_LOCATION}"
    COMPATIBILITY
    "AnyNewerVersion"
    DISABLE_VERSION_SUFFIX
    ON
    DEPENDENCIES "Threads;"
  )
endif()

# build the python binding for the library
if(${BUILD_PYTHON})
  message("Building Python bindings for the library.")
  add_subdirectory(python)
endif()

# Executables
option(CSV_BUILD_PROGRAMS "Allow to disable building of programs" ON)
if(CSV_BUILD_PROGRAMS)
  add_subdirectory("programs")
endif()

# Developer settings
if(CSV_DEVELOPER)
  # Allow for performance profiling
  if(MSVC)
    target_link_options(csv PUBLIC /PROFILE)
  endif()

  # More error messages.
  if(UNIX)
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} \
        -Wall -Wextra -Wsign-compare \
        -Wwrite-strings -Wpointer-arith -Winit-self \
        -Wconversion -Wno-sign-conversion")
  endif()

  # Generate a single header library
  if(CMAKE_VERSION VERSION_LESS "3.12")
    find_package(PythonInterp 3 QUIET)
  else()
    find_package(Python3 COMPONENTS Interpreter)
  endif()
  if(Python3_Interpreter_FOUND OR PYTHONINTERP_FOUND)
    add_custom_target(
      generate_single_header
      COMMAND ${Python3_EXECUTABLE} single_header.py > single_include/csv.hpp
      COMMAND ${Python3_EXECUTABLE} single_header.py >
              single_include_test/csv.hpp
      WORKING_DIRECTORY ${CSV_ROOT_DIR})
    # Single header compilation test
    add_subdirectory(single_include_test)
  else()
    message(
      WARNING "Python3 not found, skipping target 'generate_single_header'.")
  endif()

  # Documentation
  find_package(Doxygen QUIET)
  if(DOXYGEN_FOUND)
    add_custom_target(
      doxygen
      COMMAND ${DOXYGEN_EXECUTABLE} ${CSV_ROOT_DIR}/Doxyfile
      WORKING_DIRECTORY ${CSV_ROOT_DIR})
  else()
    message(WARNING "Doxygen not found, skipping target 'doxygen'.")
  endif()

  # Tests
  enable_testing()
  add_subdirectory("tests")

  # Code coverage find_program( GCOV_PATH gcov ) if(GCOV_PATH)
  # set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/modules")
  # include(CodeCoverage) append_coverage_compiler_flags()
  # set(ENV{CSV_TEST_ROOT} ${CSV_TEST_DIR})
  # setup_target_for_coverage_gcovr_html( NAME csv_coverage EXECUTABLE csv_test
  # EXCLUDE "tests/*" ) endif()
endif()
