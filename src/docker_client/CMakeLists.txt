add_library(docker_client STATIC
        DockerClient.cpp
)

target_include_directories(docker_client
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(docker_client
        PUBLIC
        httplib::httplib
        spdlog::spdlog
        PRIVATE
        nlohmann_json::nlohmann_json
)
