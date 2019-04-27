cmake_minimum_required(VERSION 3.9)
project(csv_parser)

set(CMAKE_CXX_STANDARD 17)

if (MSVC)
	# Make Visual Studio report accurate C++ version
	# See: https://devblogs.microsoft.com/cppblog/msvc-now-correctly-reports-__cplusplus/
	set(CMAKE_CXX_FLAGS "/EHsc /Zc:__cplusplus")

	if(CMAKE_BUILD_TYPE MATCHES Debug)
		# /Wall emits warnings about the C++ standard library
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")
	endif(CMAKE_BUILD_TYPE MATCHES Debug)

else()
	set(CMAKE_CXX_FLAGS "-pthread")
	set(CMAKE_CXX_FLAGS_RELEASE "-O3")
	set(CMAKE_CXX_FLAGS_DEBUG "-Og -g -lgcov --coverage")
endif(MSVC)

message("CSV for C++ ${CMAKE_BUILD_TYPE} Build with ${CMAKE_CXX_COMPILER}")

set(SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/include/internal/)
set(TEST_DIR ${CMAKE_CURRENT_LIST_DIR}/tests)

# file(GLOB_RECURSE SOURCES include/ *.hpp *.cpp)
set(SOURCES
	${SOURCE_DIR}/csv_reader.cpp
	${SOURCE_DIR}/csv_reader_iterator.cpp
	${SOURCE_DIR}/csv_row.cpp
	${SOURCE_DIR}/csv_stat.cpp
	${SOURCE_DIR}/csv_utility.cpp
	${SOURCE_DIR}/data_type.cpp
	${SOURCE_DIR}/giant_string_buffer.cpp
)
set(TEST_SOURCES
	${TEST_DIR}/catch.hpp
	${TEST_DIR}/main.cpp
	${TEST_DIR}/test_csv_iterator.cpp
	${TEST_DIR}/test_csv_buffer.cpp
	${TEST_DIR}/test_csv_row.cpp
	${TEST_DIR}/test_csv_stat.cpp
	${TEST_DIR}/test_read_csv.cpp
	${TEST_DIR}/test_write_csv.cpp
	${TEST_DIR}/test_data_type.cpp
)

include_directories(${CMAKE_CURRENT_LIST_DIR}/include/)
include_directories(${TEST_DIR})

## Main Library
add_library(csv STATIC ${SOURCES})
set_target_properties(csv PROPERTIES LINKER_LANGUAGE CXX)

## Executables
add_executable(csv_info ${CMAKE_CURRENT_LIST_DIR}/programs/csv_info.cpp)
target_link_libraries(csv_info csv)

add_executable(csv_bench ${CMAKE_CURRENT_LIST_DIR}/programs/csv_bench.cpp)
target_link_libraries(csv_bench csv)

add_executable(csv_guess_bench ${CMAKE_CURRENT_LIST_DIR}/programs/csv_guess_bench.cpp)
target_link_libraries(csv_guess_bench csv)

add_executable(csv_stats ${CMAKE_CURRENT_LIST_DIR}/programs/csv_stats.cpp)
target_link_libraries(csv_stats csv)

add_executable(csv_generator ${CMAKE_CURRENT_LIST_DIR}/programs/csv_generator.cpp)
target_link_libraries(csv_generator csv)

add_executable(data_type_bench ${CMAKE_CURRENT_LIST_DIR}/programs/data_type_bench.cpp)
target_link_libraries(data_type_bench csv)

## Tests
add_executable(csv_test ${TEST_SOURCES})
target_link_libraries(csv_test csv)
add_custom_command(
	TARGET csv_test POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	${TEST_DIR}/data $<TARGET_FILE_DIR:csv_test>/tests/data
)

enable_testing()
add_test(test csv_test)