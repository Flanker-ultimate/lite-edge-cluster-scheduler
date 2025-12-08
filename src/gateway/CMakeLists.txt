add_executable(gateway
        main.cpp
        HttpServer.cpp
        SocketServer.cpp
        SocketServer.h
)

target_include_directories(gateway
        INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(gateway
        PRIVATE
        scheduler
        nlohmann_json::nlohmann_json
        httplib::httplib
        thread_safe_map
        device_struct
        docker_client
        time_tools
        spdlog::spdlog
)



