cmake_minimum_required(VERSION 3.14)
project(csv_parser_comparison_benchmarks LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CSV_BENCH_FETCH_GOOGLE_BENCHMARK "Fetch Google Benchmark if it is not installed" ON)
option(CSV_BENCH_FETCH_FAST_CPP_CSV_PARSER "Fetch fast-cpp-csv-parser if no include dir is provided" ON)
option(CSV_BENCH_FETCH_RAPIDCSV "Fetch rapidcsv if no include dir is provided" ON)
option(CSV_BENCH_SMOKE_ONLY "Build only the csv-parser diagnostic smoke target" OFF)

set(FAST_CPP_CSV_PARSER_INCLUDE_DIR "" CACHE PATH "Path containing fast-cpp-csv-parser csv.h")
set(RAPIDCSV_INCLUDE_DIR "" CACHE PATH "Path containing rapidcsv.h")

include(FetchContent)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON CACHE BOOL "Do not update already-populated benchmark dependencies" FORCE)

set(CSV_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
get_filename_component(CSV_PARSER_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
add_subdirectory("${CSV_PARSER_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/csv-parser" EXCLUDE_FROM_ALL)

add_executable(csv_parser_msvc_smoke csv_parser_msvc_smoke.cpp)
target_link_libraries(csv_parser_msvc_smoke PRIVATE csv)

if(CSV_BENCH_SMOKE_ONLY)
    return()
endif()

find_package(benchmark CONFIG QUIET)
if(NOT benchmark_FOUND)
    if(NOT CSV_BENCH_FETCH_GOOGLE_BENCHMARK)
        message(FATAL_ERROR "Google Benchmark was not found. Install it or enable CSV_BENCH_FETCH_GOOGLE_BENCHMARK.")
    endif()

    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        google_benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.5
    )
    FetchContent_MakeAvailable(google_benchmark)
endif()

add_executable(csv_parser_read_bench csv_parser_read_bench.cpp)
target_link_libraries(csv_parser_read_bench PRIVATE csv benchmark::benchmark)

add_executable(csv_parser_multi_pass_bench csv_parser_multi_pass_bench.cpp)
target_link_libraries(csv_parser_multi_pass_bench PRIVATE csv benchmark::benchmark)


if(NOT FAST_CPP_CSV_PARSER_INCLUDE_DIR)
    set(_FAST_CPP_CSV_PARSER_FETCHED_DIR "${CMAKE_BINARY_DIR}/_deps/fast_cpp_csv_parser-src")
    if(EXISTS "${_FAST_CPP_CSV_PARSER_FETCHED_DIR}/csv.h")
        set(FAST_CPP_CSV_PARSER_INCLUDE_DIR "${_FAST_CPP_CSV_PARSER_FETCHED_DIR}")
    endif()
endif()

if(NOT FAST_CPP_CSV_PARSER_INCLUDE_DIR AND CSV_BENCH_FETCH_FAST_CPP_CSV_PARSER)
    FetchContent_Declare(
        fast_cpp_csv_parser
        GIT_REPOSITORY https://github.com/ben-strasser/fast-cpp-csv-parser.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(fast_cpp_csv_parser)
    set(FAST_CPP_CSV_PARSER_INCLUDE_DIR "${fast_cpp_csv_parser_SOURCE_DIR}")
endif()

if(FAST_CPP_CSV_PARSER_INCLUDE_DIR)
    add_executable(fast_cpp_csv_parser_read_bench fast_cpp_csv_parser_read_bench.cpp)
    target_include_directories(fast_cpp_csv_parser_read_bench PRIVATE "${FAST_CPP_CSV_PARSER_INCLUDE_DIR}")
    target_link_libraries(fast_cpp_csv_parser_read_bench PRIVATE benchmark::benchmark)

    add_executable(fast_cpp_csv_parser_multi_pass_bench fast_cpp_csv_parser_multi_pass_bench.cpp)
    target_include_directories(fast_cpp_csv_parser_multi_pass_bench PRIVATE "${FAST_CPP_CSV_PARSER_INCLUDE_DIR}")
    target_link_libraries(fast_cpp_csv_parser_multi_pass_bench PRIVATE benchmark::benchmark)
else()
    message(STATUS "Skipping fast-cpp-csv-parser benchmarks; set FAST_CPP_CSV_PARSER_INCLUDE_DIR or enable fetching.")
endif()

if(NOT RAPIDCSV_INCLUDE_DIR)
    set(_RAPIDCSV_FETCHED_DIR "${CMAKE_BINARY_DIR}/_deps/rapidcsv-src/src")
    if(EXISTS "${_RAPIDCSV_FETCHED_DIR}/rapidcsv.h")
        set(RAPIDCSV_INCLUDE_DIR "${_RAPIDCSV_FETCHED_DIR}")
    endif()
endif()

if(NOT RAPIDCSV_INCLUDE_DIR AND CSV_BENCH_FETCH_RAPIDCSV)
    FetchContent_Declare(
        rapidcsv
        GIT_REPOSITORY https://github.com/d99kris/rapidcsv.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(rapidcsv)
    set(RAPIDCSV_INCLUDE_DIR "${rapidcsv_SOURCE_DIR}/src")
endif()

if(RAPIDCSV_INCLUDE_DIR)
    add_executable(dataframe_rapidcsv_roundtrip_bench dataframe_rapidcsv_roundtrip_bench.cpp)
    target_include_directories(dataframe_rapidcsv_roundtrip_bench PRIVATE "${RAPIDCSV_INCLUDE_DIR}")
    target_link_libraries(dataframe_rapidcsv_roundtrip_bench PRIVATE csv benchmark::benchmark)
else()
    message(STATUS "Skipping rapidcsv benchmarks; set RAPIDCSV_INCLUDE_DIR or enable fetching.")
endif()
