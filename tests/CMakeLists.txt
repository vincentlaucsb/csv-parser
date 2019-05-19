add_executable(csv_test "")
target_sources(csv_test
	PRIVATE
		${CSV_INCLUDE_DIR}/csv.hpp
		catch.hpp
		main.cpp
		test_csv_iterator.cpp
		test_csv_buffer.cpp
		test_csv_row.cpp
		test_csv_stat.cpp
		test_read_csv.cpp
		test_write_csv.cpp
		test_data_type.cpp
)
target_link_libraries(csv_test csv)

add_test(
	NAME test
	COMMAND csv_test
	WORKING_DIRECTORY ${CSV_ROOT_DIR}
)