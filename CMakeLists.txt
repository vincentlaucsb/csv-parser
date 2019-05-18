cmake_minimum_required(VERSION 3.9)
project(csv)

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
	if(CMAKE_BUILD_TYPE MATCHES Debug)
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
	else()
		set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Og -g")
	endif(CMAKE_BUILD_TYPE MATCHES Debug)
	
	message("Using ${CMAKE_CXX_FLAGS}")
endif(MSVC)

set(CSV_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})
set(CSV_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include/)
set(CSV_SOURCE_DIR ${CSV_INCLUDE_DIR}/internal/)
set(CSV_TEST_DIR ${CMAKE_CURRENT_LIST_DIR}/tests)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

if (CMAKE_COMPILER_IS_GNUCXX)
	include(CodeCoverage)
	APPEND_COVERAGE_COMPILER_FLAGS("-lgcov --coverage")
	set(GCC_COVERAGE_LINK_FLAGS "-lgcov")
endif()

include_directories(${CSV_INCLUDE_DIR})

## Main Library
add_subdirectory(${CSV_SOURCE_DIR})

## Executables
add_subdirectory("programs")

## Tests
enable_testing()
add_subdirectory("tests")

if (CMAKE_COMPILER_IS_GNUCXX)
	setup_target_for_coverage_lcov(
		NAME ${PROJECT_NAME}_coverage
		EXECUTABLE csv_test
		DEPENDENCIES csv_test
	)
endif()