add_library(csv STATIC "")

target_sources(csv
	PRIVATE
		col_names.cpp
		col_names.hpp
		common.hpp
		csv_format.hpp
		csv_format.cpp
		csv_exceptions.hpp
		parser/core.hpp
		parser/driver.hpp
		parser/driver.cpp
		parser/guessing.cpp
		parser/mmap.hpp
		parser/mmap.cpp
		parser/orchestrator.hpp
		parser/scheduler.hpp
		parser/stream.hpp
		speculative/chunks.hpp
		speculative/chunk_parser.hpp
		speculative/diagnostics.hpp
		speculative/parallel_parser.hpp
		speculative/scanner.hpp
		speculative/validator.hpp
		csv_reader.hpp
		csv_reader.cpp
		csv_reader_iterator.cpp
		csv_row.hpp
		csv_row.cpp
		csv_utility.cpp
		csv_utility.hpp
		csv_writer.hpp
		data_type.hpp
		memory/block_arena.hpp
		memory/constants.hpp
		memory/quote_arena.hpp
		memory/raw_csv_field.hpp
		memory/raw_csv_field_list.hpp
		raw_csv_data.hpp
		row_deque.hpp
		single_thread_deque.hpp
		string_view_stream.hpp
		thread_safe_deque.hpp
	)

set_target_properties(csv PROPERTIES LINKER_LANGUAGE CXX)

if(CSV_NO_SIMD)
	target_compile_definitions(csv PUBLIC CSV_NO_SIMD=1)
endif()

if(CSV_ENABLE_THREADS)
	target_compile_definitions(csv PUBLIC CSV_ENABLE_THREADS=1)
	target_link_libraries(csv PRIVATE Threads::Threads)
else()
	target_compile_definitions(csv PUBLIC CSV_ENABLE_THREADS=0)
endif()

if(MSVC AND NOT CSV_NO_SIMD AND CSV_TARGET_X86)
	target_compile_options(csv PUBLIC /arch:AVX2)
endif()

target_include_directories(csv INTERFACE ../)

# Scalar-only variant of the library: same sources, CSV_NO_SIMD defined.
# Used by csv_bench_no_simd to provide a fair apples-to-apples benchmark.
add_library(csv_no_simd STATIC "")
get_target_property(_csv_sources csv SOURCES)
target_sources(csv_no_simd PRIVATE ${_csv_sources})
set_target_properties(csv_no_simd PROPERTIES LINKER_LANGUAGE CXX)
target_compile_definitions(csv_no_simd PUBLIC CSV_NO_SIMD=1)
if(CSV_ENABLE_THREADS)
	target_compile_definitions(csv_no_simd PUBLIC CSV_ENABLE_THREADS=1)
	target_link_libraries(csv_no_simd PRIVATE Threads::Threads)
else()
	target_compile_definitions(csv_no_simd PUBLIC CSV_ENABLE_THREADS=0)
endif()
target_include_directories(csv_no_simd INTERFACE ../)
