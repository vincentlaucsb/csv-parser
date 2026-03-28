# The purpose of this executable is to make sure it successfully compiles
add_executable(single_include_test "")
target_sources(single_include_test
    PRIVATE
        file1.cpp
        file2.cpp
)
add_dependencies(single_include_test generate_single_header)
target_include_directories(single_include_test PRIVATE ${CSV_SINGLE_INCLUDE_GENERATED_DIR})
target_link_libraries(single_include_test PRIVATE Threads::Threads)
