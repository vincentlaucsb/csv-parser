cmake_minimum_required(VERSION 3.9)
project(SVG)

set(CMAKE_CXX_STANDARD 14)
set(SOURCES src/csv_reader.cpp src/csv_stat.cpp src/csv_writer.cpp)

include_directories(${CMAKE_SOURCE_DIR}/src/)
include_directories(${CMAKE_SOURCE_DIR}/tests/)
add_executable(CSV_Test ${SOURCES}
	tests/catch.hpp
	tests/main.cpp
	tests/test_csv_stat.cpp
	tests/test_read_csv.cpp
	tests/test_write_csv.cpp
)

enable_testing()
add_test(test CSV_TEST)