add_library(csv STATIC "")

target_sources(csv
	PRIVATE
		compatibility.hpp
		constants.hpp
		csv_format.hpp
		csv_reader.hpp
		csv_reader.cpp
		csv_reader_iterator.cpp
		csv_row.hpp
		csv_row.cpp
		csv_stat.cpp
		csv_stat.hpp
		csv_utility.cpp
		csv_utility.hpp
		data_type.h
		giant_string_buffer.hpp
		giant_string_buffer.cpp
)

set_target_properties(csv PROPERTIES LINKER_LANGUAGE CXX)