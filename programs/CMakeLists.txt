add_executable(csv_info ${CMAKE_CURRENT_LIST_DIR}/csv_info.cpp)
target_link_libraries(csv_info csv)

add_executable(csv_bench ${CMAKE_CURRENT_LIST_DIR}/csv_bench.cpp)
target_link_libraries(csv_bench csv)

add_executable(csv_guess_bench ${CMAKE_CURRENT_LIST_DIR}/csv_guess_bench.cpp)
target_link_libraries(csv_guess_bench csv)

add_executable(csv_stats ${CMAKE_CURRENT_LIST_DIR}/csv_stats.cpp)
target_link_libraries(csv_stats csv)

add_executable(csv_generator ${CMAKE_CURRENT_LIST_DIR}/csv_generator.cpp)
target_link_libraries(csv_generator csv)

add_executable(data_type_bench ${CMAKE_CURRENT_LIST_DIR}/data_type_bench.cpp)
target_link_libraries(data_type_bench csv)