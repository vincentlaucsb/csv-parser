include(FetchContent)

set(CSVPY_PYBIND11_GIT_REPOSITORY "https://github.com/pybind/pybind11.git" CACHE STRING
    "pybind11 Git repository used when building csvpy")
set(CSVPY_PYBIND11_GIT_TAG "v3.0.3" CACHE STRING
    "pybind11 Git tag used when building csvpy")

set(PYBIND11_FINDPYTHON ON CACHE BOOL "Use CMake FindPython for pybind11" FORCE)
set(PYBIND11_INSTALL OFF CACHE BOOL "Disable pybind11 install rules" FORCE)
set(PYBIND11_TEST OFF CACHE BOOL "Disable pybind11 tests" FORCE)

find_package(pybind11 CONFIG QUIET)
if(NOT pybind11_FOUND)
    FetchContent_Declare(
        pybind11
        GIT_REPOSITORY ${CSVPY_PYBIND11_GIT_REPOSITORY}
        GIT_TAG ${CSVPY_PYBIND11_GIT_TAG}
    )
    FetchContent_MakeAvailable(pybind11)
endif()

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
if(BUILD_PYTHON AND CSVPY_TEST_PYTHON_EXECUTABLE)
    enable_testing()
    add_test(
        NAME csvpy_compat_tests
        COMMAND ${CSVPY_TEST_PYTHON_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_LIST_DIR}/tests
    )
    set_tests_properties(csvpy_compat_tests PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_LIST_DIR}"
    )
endif()
