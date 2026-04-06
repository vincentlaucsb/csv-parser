# The purpose of this executable is to make sure it successfully compiles
add_executable(single_include_test "")
target_sources(single_include_test
    PRIVATE
        file1.cpp
        file2.cpp
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
