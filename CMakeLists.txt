cmake_minimum_required(VERSION 3.17)
project(docker_scheduler)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

set(DEPS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty")

FetchContent_Declare(
        googletest
        SOURCE_DIR "${DEPS_ROOT}/googletest-src"
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
        httplib
        SOURCE_DIR "${DEPS_ROOT}/httplib-src"
)

FetchContent_Declare(
        json
        SOURCE_DIR "${DEPS_ROOT}/json-src"
)

fetchcontent_declare(
        boost-uuid
        SOURCE_DIR "${DEPS_ROOT}/boost-uuid-src"
)

FetchContent_Declare(
        boost-assert
        SOURCE_DIR "${DEPS_ROOT}/boost-assert-src"
)

FetchContent_Declare(
        boost-config
        SOURCE_DIR "${DEPS_ROOT}/boost-config-src"
)

FetchContent_Declare(
        boost-throw_exception
        SOURCE_DIR "${DEPS_ROOT}/boost-throw-src"
)

FetchContent_Declare(
        boost-type_traits
        SOURCE_DIR "${DEPS_ROOT}/boost-traits-src"
)
FetchContent_Declare(
        boost-static_assert
        SOURCE_DIR "${DEPS_ROOT}/boost-static-src"
)

FetchContent_MakeAvailable(boost-uuid boost-assert boost-config boost-throw_exception boost-type_traits boost-static_assert)

FetchContent_Declare(
        z3
        SOURCE_DIR "${DEPS_ROOT}/z3-src"
)

FetchContent_Declare(
        spdlog
        SOURCE_DIR "${DEPS_ROOT}/spdlog-src"
)

FetchContent_MakeAvailable(googletest httplib json z3 spdlog)


if (WIN32)
    add_compile_definitions(WIN32_LEAN_AND_MEAN)
endif ()


get_filename_component(ABSOLUTE_CONFIG_PATH "${CMAKE_CURRENT_SOURCE_DIR}/config_files" ABSOLUTE)
add_subdirectory(src/time_tools)
add_subdirectory(src/custom_struct)
add_subdirectory(src/docker_client)
add_subdirectory(src/scheduler)
add_subdirectory(src/docker_scheduler_agent)
add_subdirectory(src/gateway)
add_subdirectory(src/z3_test)
target_compile_definitions(gateway PUBLIC ABSOLUTE_CONFIG_PATH="${ABSOLUTE_CONFIG_PATH}")
enable_testing()
include(GoogleTest)
add_subdirectory(tests/docker_client)
# add_subdirectory(tests/scheduler)
add_subdirectory(tests/z3_mock_test)
# add_subdirectory(tests/predict)
# 构建gtest end
