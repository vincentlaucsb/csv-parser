add_executable(csv_info ${CMAKE_CURRENT_LIST_DIR}/csv_info.cpp)
target_link_libraries(csv_info csv)

add_executable(csv_stats ${CMAKE_CURRENT_LIST_DIR}/csv_stats.cpp)
target_link_libraries(csv_stats csv)

# Provide rudimentary benchmarks
if(CSV_DEVELOPER)
	add_executable(csv_guess_bench ${CMAKE_CURRENT_LIST_DIR}/csv_guess_bench.cpp)
	target_link_libraries(csv_guess_bench csv)

	# Benchmarks for parsing speed
	add_executable(csv_bench ${CMAKE_CURRENT_LIST_DIR}/csv_bench.cpp)
	target_link_libraries(csv_bench csv)

	add_custom_target(generate_csv_bench
		COMMAND csv_bench 2015_StateDepartment.csv
		WORKING_DIRECTORY ${CSV_TEST_DIR}/data/real_data
	)

	# Don't compile programs if required headers are not found
	include(CheckCXXSourceCompiles)
	check_cxx_source_compiles("
		#include <charconv>
		
		int main(int argc, char** argv) {
			return 0;
		}
	" haveCharconv)

	if(haveCharconv)
		add_executable(csv_generator ${CMAKE_CURRENT_LIST_DIR}/csv_generator.cpp)
		target_link_libraries(csv_generator csv)

		# Benchmarks for data_type() function
		add_executable(data_type_bench ${CMAKE_CURRENT_LIST_DIR}/data_type_bench.cpp)
		target_link_libraries(data_type_bench csv)

		add_custom_target(generate_dtype_bench
			COMMAND data_type_bench 2015_StateDepartment.csv "Regular Pay"
			WORKING_DIRECTORY ${CSV_TEST_DIR}/data/real_data)
    endif()
endif()