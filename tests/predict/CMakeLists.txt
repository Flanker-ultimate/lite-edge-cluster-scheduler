add_executable(predict_test
        predict.cpp
)

# 添加 ONNX Runtime 的库文件路径
target_link_libraries(predict_test
        PRIVATE
        GTest::gtest_main
        scheduler
)

# 设置 ONNX Runtime 的头文件路径
target_include_directories(predict_test
        PRIVATE
)

# 使用 gtest 自动发现测试
gtest_discover_tests(predict_test)

