include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.6.0
) 

FetchContent_MakeAvailable(Catch2)

# Catch2's benchmark source files have -Wconversion hits; suppress on its targets
# so our global -Wconversion/-Werror don't fail Catch2's own compilation.
if(NOT MSVC)
    foreach(_catch2_target Catch2 Catch2WithMain)
        if(TARGET ${_catch2_target})
            target_compile_options(${_catch2_target} PRIVATE -Wno-conversion)
        endif()
    endforeach()
endif()

# Suppress warnings originating from Catch2 headers (they are not our code)
get_target_property(_catch2_inc Catch2::Catch2 INTERFACE_INCLUDE_DIRECTORIES)

function(add_csv_parser_test_target target_name)
    add_executable(${target_name} "")
    target_sources(${target_name}
        PRIVATE
            ${CSV_INCLUDE_DIR}/csv.hpp
            main.cpp
            ${ARGN}
        )

    target_link_libraries(${target_name} csv)
    target_link_libraries(${target_name} Catch2::Catch2WithMain)

    if(_catch2_inc)
        target_include_directories(${target_name} SYSTEM PRIVATE ${_catch2_inc})
    endif()

    # Emscripten: Catch2 uses __COUNTER__ (unsupported in emcc, flagged as C2y extension)
    if(EMSCRIPTEN)
        target_compile_options(${target_name} PRIVATE -Wno-c2y-extensions)
    endif()

    if(MSVC)
        # Workaround to enable debugging unit tests in Visual Studio
        add_custom_command(
            TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CSV_TEST_DIR}/data $<TARGET_FILE_DIR:${target_name}>/tests/data
        )
    endif()

    add_test(
        NAME ${target_name}
        COMMAND ${target_name} --verbosity high --durations yes
        WORKING_DIRECTORY ${CSV_ROOT_DIR}
    )
endfunction()

add_csv_parser_test_target(csv_test
    test_col_names.cpp
    test_csv_delimiter.cpp
    test_csv_field.cpp
    test_csv_field_array.cpp
    test_csv_format.cpp
    test_csv_iterator.cpp
    test_csv_ranges.cpp
    test_csv_row_offsets.cpp
    test_csv_row.cpp
    test_csv_row_json.cpp
    test_speculative_parser.cpp
    test_data_type.cpp
    test_edge_cases_large_rows.cpp
    test_error_handling.cpp
    test_guess_csv.cpp
    test_indexed_task_pool.cpp
    test_parser_edge_cases.cpp
    test_raw_csv_data.cpp
    test_read_csv.cpp
    test_read_csv_file.cpp
    test_round_trip.cpp
    test_stream_sources.cpp
)

if(NOT CSV_NO_SIMD)
    target_sources(csv_test PRIVATE test_basic_csv_parser_simd.cpp)
endif()

if(CSV_ENABLE_THREADS)
    target_sources(csv_test PRIVATE test_threadsafe_deque_race.cpp)
endif()

add_csv_parser_test_target(csv_write_test
    test_write_csv.cpp
)

add_csv_parser_test_target(data_frame_test
    test_data_frame.cpp
    test_data_frame_etl.cpp
)
