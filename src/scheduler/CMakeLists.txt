add_library(scheduler
        scheduler.cpp
)

target_include_directories(scheduler
        INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
        PUBLIC
        "${Z3_SOURCE_DIR}/src/api/c++"
#        /root/yuezhixin/onnxruntime-linux-aarch64-1.20.1/include
)

target_link_libraries(scheduler
        PUBLIC
        httplib::httplib
        nlohmann_json::nlohmann_json
        device_struct
        libz3
        spdlog::spdlog
        PRIVATE
        docker_client
        time_tools
        Boost::uuid
        Boost::assert Boost::config Boost::throw_exception Boost::type_traits Boost::static_assert
#        /root/yuezhixin/onnxruntime-linux-aarch64-1.20.1/lib/libonnxruntime.so
)
##复制模型文件
#add_custom_command(TARGET scheduler POST_BUILD
#        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/src/scheduler/predict.onnx ${CMAKE_BINARY_DIR}/src/scheduler/predict.onnx
#)