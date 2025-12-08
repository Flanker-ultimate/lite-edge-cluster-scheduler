add_executable(z3_mock_test
        z3_mock_test.cpp
)

target_link_libraries(z3_mock_test
        PRIVATE
        GTest::gtest_main
        scheduler
        time_tools
        spdlog::spdlog
)

target_include_directories(
        z3_mock_test
        PRIVATE
        "${Z3_SOURCE_DIR}/src/api/c++"
)

gtest_discover_tests(z3_mock_test)