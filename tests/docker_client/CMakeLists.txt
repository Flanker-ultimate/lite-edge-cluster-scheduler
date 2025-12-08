add_executable(docker_client_test
        docker_client_test.cpp
)

target_link_libraries(docker_client_test
        PRIVATE
        GTest::gtest_main
        docker_client
        time_tools
)

gtest_discover_tests(docker_client_test)
