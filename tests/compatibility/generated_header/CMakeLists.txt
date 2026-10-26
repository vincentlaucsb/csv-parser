cmake_minimum_required(VERSION 3.15)
project(csv_generated_header_tests LANGUAGES CXX)

if(NOT CSV_SOURCE_ROOT OR NOT CSV_GENERATED_INCLUDE_DIR)
    message(FATAL_ERROR "Set CSV_SOURCE_ROOT and CSV_GENERATED_INCLUDE_DIR")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(CSV_ENABLE_THREADS "Exercise threaded parsing" ON)
option(CSV_TEST_EXCEPTIONS "Expected compiler exception mode (supply compiler flags separately)" ON)
if(CSV_TEST_EXCEPTIONS)
    set(CATCH_CONFIG_DISABLE_EXCEPTIONS OFF CACHE BOOL "" FORCE)
else()
    set(CATCH_CONFIG_DISABLE_EXCEPTIONS ON CACHE BOOL "" FORCE)
endif()
find_package(Threads REQUIRED)
include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.6.0 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

# These existing cases exercise the distributed header without linking the library.
# Round trips use distinct fields and mmap/stream SECTIONs across 10 MB boundaries.
add_executable(csv_generated_header_tests
    ${CSV_SOURCE_ROOT}/tests/test_csv_field.cpp
    ${CSV_SOURCE_ROOT}/tests/test_csv_format.cpp
    ${CSV_SOURCE_ROOT}/tests/test_csv_row.cpp
    ${CSV_SOURCE_ROOT}/tests/test_read_csv_file.cpp
    ${CSV_SOURCE_ROOT}/tests/test_write_csv.cpp
    ${CSV_SOURCE_ROOT}/tests/test_data_frame.cpp
    ${CSV_SOURCE_ROOT}/tests/test_data_frame_dirty.cpp
    ${CSV_SOURCE_ROOT}/tests/test_data_frame_etl.cpp)
if(CSV_TEST_EXCEPTIONS)
    target_sources(csv_generated_header_tests PRIVATE
        ${CSV_SOURCE_ROOT}/tests/test_csv_iterator.cpp
        ${CSV_SOURCE_ROOT}/tests/test_round_trip.cpp)
endif()
target_include_directories(csv_generated_header_tests PRIVATE
    ${CSV_GENERATED_INCLUDE_DIR} ${CSV_SOURCE_ROOT}/tests)
target_compile_definitions(csv_generated_header_tests PRIVATE
    CSV_ENABLE_THREADS=$<BOOL:${CSV_ENABLE_THREADS}>
    CSV_TEST_EXCEPTIONS=$<BOOL:${CSV_TEST_EXCEPTIONS}> CATCH_CONFIG_NO_CPP17_STRING_VIEW)
target_link_libraries(csv_generated_header_tests PRIVATE Catch2::Catch2WithMain Threads::Threads)
if(MSVC)
    target_compile_options(csv_generated_header_tests PRIVATE /bigobj)
    if(CSV_TEST_EXCEPTIONS)
        target_compile_options(csv_generated_header_tests PRIVATE /EHsc)
    endif()
endif()
enable_testing()
add_test(NAME generated_header_regressions COMMAND csv_generated_header_tests)
set_tests_properties(generated_header_regressions PROPERTIES
    WORKING_DIRECTORY ${CSV_SOURCE_ROOT})
