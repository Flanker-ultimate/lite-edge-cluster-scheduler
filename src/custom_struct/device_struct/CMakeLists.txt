add_library(device_struct STATIC
        device.cpp
)
## 让编译器知道  第三方库的 头文件搜素路径
## 下面这个语句的意思是让任何使用custom_struct的目标  都从custom_struct cmakeList所在的目录查找头文件
target_include_directories(device_struct
        INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(device_struct
        time_tools
        nlohmann_json::nlohmann_json
        Boost::uuid
        Boost::assert Boost::config Boost::throw_exception Boost::type_traits Boost::static_assert
)

# Add the Boost include directory to your target
target_include_directories(device_struct PRIVATE
        ${boost-uuid_SOURCE_DIR}/boost
)