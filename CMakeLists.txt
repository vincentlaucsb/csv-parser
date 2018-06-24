cmake_minimum_required(VERSION 3.9)
project(csv_parser)

set(CMAKE_CXX_STANDARD 11)

if (MSVC)
else()
	set(CMAKE_CXX_FLAGS "-pthread")
	set(CMAKE_CXX_FLAGS_DEBUG "-Og -g -lgcov --coverage")
endif(MSVC)

set(SOURCES src/csv_reader.cpp src/csv_stat.cpp)
set(TEST_SOURCES
	tests/catch.hpp
	tests/main.cpp
	tests/test_data_type.cpp
	tests/test_csv_stat.cpp
	tests/test_read_csv.cpp
	tests/test_write_csv.cpp
)

include_directories(${CMAKE_SOURCE_DIR}/src/)
include_directories(${CMAKE_SOURCE_DIR}/tests/)

## Main Library
add_library(csv ${SOURCES})
set_target_properties(csv PROPERTIES LINKER_LANGUAGE CXX)

## Executables
add_executable(csv_info programs/csv_info.cpp)
target_link_libraries(csv_info csv)

add_executable(csv_bench programs/csv_bench.cpp)
target_link_libraries(csv_bench csv)

add_executable(csv_stats programs/csv_stats.cpp)
target_link_libraries(csv_stats csv)

## Tests
add_executable(csv_test ${SOURCES} ${TEST_SOURCES})

enable_testing()
add_test(test csv_test)