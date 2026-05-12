include(FetchContent)

set(FASTPYCSV_NANOBIND_GIT_REPOSITORY "https://github.com/wjakob/nanobind.git" CACHE STRING
    "nanobind Git repository used when building fastpycsv")
set(FASTPYCSV_NANOBIND_GIT_TAG "v2.12.0" CACHE STRING
    "nanobind Git tag used when building fastpycsv")

if(CMAKE_VERSION VERSION_LESS 3.18)
    set(FASTPYCSV_PYTHON_DEV_MODULE Development)
else()
    set(FASTPYCSV_PYTHON_DEV_MODULE Development.Module)
endif()

find_package(Python 3.9 COMPONENTS Interpreter ${FASTPYCSV_PYTHON_DEV_MODULE} REQUIRED)

find_package(nanobind CONFIG QUIET)
if(NOT nanobind_FOUND)
    FetchContent_Declare(
        nanobind
        GIT_REPOSITORY ${FASTPYCSV_NANOBIND_GIT_REPOSITORY}
        GIT_TAG ${FASTPYCSV_NANOBIND_GIT_TAG}
    )
    FetchContent_MakeAvailable(nanobind)
endif()

set(FASTPYCSV_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/fastpycsv.cpp
    ${CMAKE_CURRENT_LIST_DIR}/fastpycsv_core.cpp
    ${CMAKE_CURRENT_LIST_DIR}/fastpycsv_format.cpp
    ${CMAKE_CURRENT_LIST_DIR}/fastpycsv_numpy.cpp
    ${CMAKE_CURRENT_LIST_DIR}/fastpycsv_predicate.cpp
    ${CMAKE_CURRENT_LIST_DIR}/fastpycsv_reader.cpp
    ${CMAKE_CURRENT_LIST_DIR}/fastpycsv_writer.cpp
)

nanobind_add_module(fastpycsv NOMINSIZE ${FASTPYCSV_SOURCES})
target_link_libraries(fastpycsv PUBLIC csv)
install(TARGETS fastpycsv
        LIBRARY DESTINATION fastpycsv
        RUNTIME DESTINATION fastpycsv)

get_property(fastpycsvSuffix TARGET fastpycsv PROPERTY SUFFIX)
add_custom_command(TARGET fastpycsv POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:fastpycsv>
                   ${CMAKE_CURRENT_LIST_DIR}/fastpycsv/fastpycsv${fastpycsvSuffix})

if(DEFINED PYTHON_EXECUTABLE)
    set(FASTPYCSV_TEST_PYTHON_EXECUTABLE ${PYTHON_EXECUTABLE})
else()
    set(FASTPYCSV_TEST_PYTHON_EXECUTABLE ${Python_EXECUTABLE})
endif()
if(BUILD_PYTHON AND FASTPYCSV_TEST_PYTHON_EXECUTABLE)
    enable_testing()
    add_test(
        NAME fastpycsv_compat_tests
        COMMAND ${FASTPYCSV_TEST_PYTHON_EXECUTABLE} -m unittest discover -s ${CMAKE_CURRENT_LIST_DIR}/tests
    )
    set_tests_properties(fastpycsv_compat_tests PROPERTIES
        ENVIRONMENT "PYTHONPATH=${CMAKE_CURRENT_LIST_DIR}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
    )
endif()
