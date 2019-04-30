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
add_custom_command(
	TARGET csv_test POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E copy_directory
	data $<TARGET_FILE_DIR:csv_test>/tests/data
)

enable_testing()
add_test(test csv_test)