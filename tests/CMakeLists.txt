include(FetchContent)

FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.6.0
  GIT_SHALLOW TRUE
) 

if(NOT CSV_ENABLE_EXCEPTIONS)
    # Consistent framework behavior for assertions from no-exception test TUs.
    set(CATCH_CONFIG_DISABLE_EXCEPTIONS ON CACHE BOOL "" FORCE)
else()
    set(CATCH_CONFIG_DISABLE_EXCEPTIONS OFF CACHE BOOL "" FORCE)
endif()
FetchContent_MakeAvailable(Catch2)
if(MSVC AND NOT CSV_ENABLE_EXCEPTIONS)
    target_compile_options(Catch2 PRIVATE /EHsc)
    target_compile_options(Catch2WithMain PRIVATE /EHsc)
endif()

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

# Catch2 v3 upgrades its consumers to C++14. Do not mix standard-dependent
# inline definitions from those TUs with a C++11 library. The separate smoke
# consumer below still links the real C++11 target and uses no test framework.
set(_csv_catch_library csv)
if(CMAKE_CXX_STANDARD LESS 14)
    get_target_property(_csv_catch_sources csv SOURCES)
    get_target_property(_csv_source_root csv SOURCE_DIR)
    list(TRANSFORM _csv_catch_sources PREPEND "${_csv_source_root}/")
    add_library(csv_catch_cxx14 STATIC ${_csv_catch_sources})
    set_target_properties(csv_catch_cxx14 PROPERTIES CXX_STANDARD 14)
    target_include_directories(csv_catch_cxx14 PUBLIC "${CSV_INCLUDE_DIR}")
    target_compile_definitions(csv_catch_cxx14 PUBLIC
        $<TARGET_PROPERTY:csv,INTERFACE_COMPILE_DEFINITIONS>)
    target_compile_options(csv_catch_cxx14 PUBLIC
        $<TARGET_PROPERTY:csv,INTERFACE_COMPILE_OPTIONS>)
    if(CSV_ENABLE_THREADS)
        target_link_libraries(csv_catch_cxx14 PUBLIC Threads::Threads)
    endif()
    set(_csv_catch_library csv_catch_cxx14)
endif()

function(add_csv_parser_test_target target_name)
    add_executable(${target_name} "")
    target_sources(${target_name}
        PRIVATE
            ${CSV_INCLUDE_DIR}/csv.hpp
            main.cpp
            ${ARGN}
        )

    target_link_libraries(${target_name} ${_csv_catch_library})
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

    set(_csv_test_filters)
    if(ENABLE_CODE_COVERAGE)
        # Repeated stress loops add little line coverage and become brittle
        # under instrumentation. Normal and sanitizer jobs still run them.
        list(APPEND _csv_test_filters "~[stress]")
    endif()

    add_test(
        NAME ${target_name}
        COMMAND ${target_name} ${_csv_test_filters} --verbosity high --durations yes
        WORKING_DIRECTORY ${CSV_ROOT_DIR}
    )
endfunction()

# Exercise the explicit-error API in both compiler modes.
add_csv_parser_test_target(csv_error_test test_explicit_errors.cpp)
set_tests_properties(csv_error_test PROPERTIES TIMEOUT 120)
target_compile_definitions(csv_error_test PRIVATE
    CSV_TEST_EXCEPTIONS=$<BOOL:${CSV_ENABLE_EXCEPTIONS}>
    CSV_TEST_CXX_STANDARD=${CMAKE_CXX_STANDARD})

# Catch2 v3 requires C++14. This separate consumer really uses the requested
# standard, including C++11, and checks cross-TU linkage.
add_executable(csv_error_api_smoke
    compatibility/explicit_errors/smoke.cpp
    compatibility/explicit_errors/second.cpp)
target_link_libraries(csv_error_api_smoke PRIVATE csv)
target_compile_definitions(csv_error_api_smoke PRIVATE
    CSV_TEST_EXCEPTIONS=$<BOOL:${CSV_ENABLE_EXCEPTIONS}>
    CSV_TEST_CXX_STANDARD=${CMAKE_CXX_STANDARD})
if(CMAKE_CXX_STANDARD GREATER_EQUAL 23)
    target_compile_definitions(csv_error_api_smoke PRIVATE CSV_TEST_EXPECT_EXPECTED=1)
endif()
add_test(NAME csv_error_api_smoke COMMAND csv_error_api_smoke)

if(CSV_ENABLE_EXCEPTIONS)
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
    test_data_frame_dirty.cpp
    test_data_frame_etl.cpp
)

endif()
