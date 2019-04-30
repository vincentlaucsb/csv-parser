add_library(csv STATIC "")

target_sources(csv
	PRIVATE
		csv_reader.cpp
		csv_reader_iterator.cpp
		csv_row.cpp
		csv_stat.cpp
		csv_utility.cpp
		data_type.cpp
		giant_string_buffer.cpp
)

set_target_properties(csv PROPERTIES LINKER_LANGUAGE CXX)