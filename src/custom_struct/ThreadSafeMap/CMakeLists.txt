## 创建第三方库
add_library(thread_safe_map STATIC
        Map.cpp
        ThreadSafeMap.cpp
)
## 让编译器知道  第三方库的 头文件搜素路径
## 下面这个语句的意思是让任何使用custom_struct的目标  都从custom_struct cmakeList所在的目录查找头文件
target_include_directories(thread_safe_map
        INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)