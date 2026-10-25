# The purpose of this executable is to make sure it successfully compiles
add_executable(single_include_test "")
target_sources(single_include_test
    PRIVATE
        ${CSV_ROOT_DIR}/tests/compatibility/explicit_errors/smoke.cpp
        ${CSV_ROOT_DIR}/tests/compatibility/explicit_errors/second.cpp
)
add_dependencies(single_include_test generate_single_header)
# Ensure "csv.hpp" resolves to the generated amalgamated header, not include/csv.hpp.
target_include_directories(single_include_test BEFORE PRIVATE ${CSV_SINGLE_INCLUDE_GENERATED_DIR})

if(CSV_ENABLE_THREADS)
    target_compile_definitions(single_include_test PRIVATE CSV_ENABLE_THREADS=1)
    target_link_libraries(single_include_test PRIVATE Threads::Threads)
else()
    target_compile_definitions(single_include_test PRIVATE CSV_ENABLE_THREADS=0)
endif()

csv_configure_exceptions(single_include_test PRIVATE)
target_compile_definitions(single_include_test PRIVATE
    CSV_TEST_EXCEPTIONS=$<BOOL:${CSV_ENABLE_EXCEPTIONS}>
    CSV_TEST_CXX_STANDARD=${CMAKE_CXX_STANDARD})
if(CMAKE_CXX_STANDARD GREATER_EQUAL 23)
    target_compile_definitions(single_include_test PRIVATE CSV_TEST_EXPECT_EXPECTED=1)
endif()
