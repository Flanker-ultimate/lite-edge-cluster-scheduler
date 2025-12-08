add_executable(scheduler_test
        scheduler_test.cpp
)

target_link_libraries(scheduler_test
        PRIVATE
        GTest::gtest_main
        scheduler
        time_recorder
)

gtest_discover_tests(scheduler_test)