cmake_minimum_required(VERSION 3.9)
project(csv_parser)

set(CMAKE_CXX_STANDARD 17)

if (MSVC)
else()
	set(CMAKE_CXX_FLAGS "-pthread")
	set(CMAKE_CXX_FLAGS_RELEASE "-O3")
	set(CMAKE_CXX_FLAGS_DEBUG "-Og -g -lgcov --coverage")
endif(MSVC)

set(SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(TEST_DIR ${CMAKE_CURRENT_LIST_DIR}/tests)
set(SOURCES
    ${SOURCE_DIR}/csv_reader.cpp
	${SOURCE_DIR}/csv_row.cpp
    ${SOURCE_DIR}/csv_stat.cpp
	${SOURCE_DIR}/data_type.cpp
)
set(TEST_SOURCES
	${TEST_DIR}/catch.hpp
	${TEST_DIR}/main.cpp
	${TEST_DIR}/test_csv_row.cpp
	${TEST_DIR}/test_data_type.cpp
	${TEST_DIR}/test_csv_stat.cpp
	${TEST_DIR}/test_read_csv.cpp
	${TEST_DIR}/test_write_csv.cpp
)

include_directories(${SOURCE_DIR})
include_directories(${TEST_DIR})

## Main Library
add_library(csv STATIC ${SOURCES})
set_target_properties(csv PROPERTIES LINKER_LANGUAGE CXX)

## Executables
add_executable(csv_info ${CMAKE_CURRENT_LIST_DIR}/programs/csv_info.cpp)
target_link_libraries(csv_info csv)

add_executable(csv_bench ${CMAKE_CURRENT_LIST_DIR}/programs/csv_bench.cpp)
target_link_libraries(csv_bench csv)

add_executable(csv_stats ${CMAKE_CURRENT_LIST_DIR}/programs/csv_stats.cpp)
target_link_libraries(csv_stats csv)

## Tests
add_executable(csv_test ${SOURCES} ${TEST_SOURCES})

enable_testing()
add_test(test csv_test)