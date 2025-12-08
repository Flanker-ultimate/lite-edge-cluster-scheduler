#include "HttpServer.h"
#include "scheduler.h"
#include "SocketServer.h"
#include <thread>
//
// Created by lxsa1 on 19/10/2024.
//

int main() {
    std::string absoulte_config_path = std::string(ABSOLUTE_CONFIG_PATH);
    Docker_scheduler::init(absoulte_config_path + "/static_info.json");
    Docker_scheduler::startDeviceInfoCollection();

    spdlog::set_level(spdlog::level::info);

    // create http server
    std::thread([absoulte_config_path]() {
        const std::string addr = "0.0.0.0";
        const int port = 6666;
        HttpServer http_server(addr, port, absoulte_config_path);
        http_server.Start();
    }).detach();

    const std::string addr = "0.0.0.0";
    const int port = 7777;
    SocketServer sock_server(addr, port);
    sock_server.Start();

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    return 0;
}
