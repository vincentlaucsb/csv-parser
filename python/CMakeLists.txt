if (EXISTS ${CMAKE_CURRENT_LIST_DIR}/pybind11)
    add_subdirectory(pybind11)

    if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
        set(PYBIND11_CPP_STANDARD /std:c++17)
    else()
        set(PYBIND11_CPP_STANDARD -std=c++1z)
    endif()

    set(CSVPY_SOURCES 
        ${CMAKE_CURRENT_LIST_DIR}/csvpy.cpp
    )

    pybind11_add_module(csvpy ${CSVPY_SOURCES})
    target_link_libraries(csvpy PUBLIC csv)

    get_property(csvpySuffix TARGET csvpy PROPERTY SUFFIX)
    add_custom_command(TARGET csvpy POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:csvpy>
                       ${CMAKE_CURRENT_LIST_DIR}/csvpy/csvpy${csvpySuffix})

    if(DEFINED PYTHON_EXECUTABLE)
        set(CSVPY_TEST_PYTHON_EXECUTABLE ${PYTHON_EXECUTABLE})
    else()
        find_package(Python3 COMPONENTS Interpreter QUIET)
        if(Python3_Interpreter_FOUND)
            set(CSVPY_TEST_PYTHON_EXECUTABLE ${Python3_EXECUTABLE})
        endif()
    endif()
    if(CSVPY_TEST_PYTHON_EXECUTABLE)
        enable_testing()
        add_test(
            NAME csvpy_compat_tests
            COMMAND ${CSVPY_TEST_PYTHON_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_LIST_DIR}/tests
        )
        set_tests_properties(csvpy_compat_tests PROPERTIES
            ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_LIST_DIR}"
        )
    endif()
else()
    message("Cannot build Python bindings because pybind11 submodule was not found. Please run \"git submodule update --recursive\".")
endif()
