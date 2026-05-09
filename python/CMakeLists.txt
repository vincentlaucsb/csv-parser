include(FetchContent)

set(CSVPY_NANOBIND_GIT_REPOSITORY "https://github.com/wjakob/nanobind.git" CACHE STRING
    "nanobind Git repository used when building csvpy")
set(CSVPY_NANOBIND_GIT_TAG "v2.12.0" CACHE STRING
    "nanobind Git tag used when building csvpy")

if(CMAKE_VERSION VERSION_LESS 3.18)
    set(CSVPY_PYTHON_DEV_MODULE Development)
else()
    set(CSVPY_PYTHON_DEV_MODULE Development.Module)
endif()

find_package(Python 3.9 COMPONENTS Interpreter ${CSVPY_PYTHON_DEV_MODULE} REQUIRED)

find_package(nanobind CONFIG QUIET)
if(NOT nanobind_FOUND)
    FetchContent_Declare(
        nanobind
        GIT_REPOSITORY ${CSVPY_NANOBIND_GIT_REPOSITORY}
        GIT_TAG ${CSVPY_NANOBIND_GIT_TAG}
    )
    FetchContent_MakeAvailable(nanobind)
endif()

set(CSVPY_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/csvpy.cpp
)

nanobind_add_module(csvpy NOMINSIZE ${CSVPY_SOURCES})
target_link_libraries(csvpy PUBLIC csv)

get_property(csvpySuffix TARGET csvpy PROPERTY SUFFIX)
add_custom_command(TARGET csvpy POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:csvpy>
                   ${CMAKE_CURRENT_LIST_DIR}/csvpy/csvpy${csvpySuffix})

if(DEFINED PYTHON_EXECUTABLE)
    set(CSVPY_TEST_PYTHON_EXECUTABLE ${PYTHON_EXECUTABLE})
else()
    set(CSVPY_TEST_PYTHON_EXECUTABLE ${Python_EXECUTABLE})
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
