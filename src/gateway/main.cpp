#include "HttpServer.h"
#include "scheduler.h"
#include <thread>
//
// Created by lxsa1 on 19/10/2024.
//

Args parse_arguments(int argc, char* argv[]) {
    Args args;
    args.config_path = "./myapp";  
    args.task_path = "./tasks";
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            args.config_path = argv[++i];
        } 
        else if (arg == "--task" && i + 1 < argc) {
            args.task_path = argv[++i];
        }
        else if (arg == "-c" && i + 1 < argc) {  // 短选项
            args.config_path = argv[++i];
        }
        else if (arg == "-t" && i + 1 < argc) {  // 短选项
            args.task_path = argv[++i];
        }
    }
    return args;
}

int main(int argc, char* argv[]) {
    Args args = parse_arguments(argc, argv);
    spdlog::info("parse params config_path: {}, task_path: {}", args.config_path, args.task_path);
    Docker_scheduler::init(args.config_path + "/static_info.json");
    Docker_scheduler::startDeviceInfoCollection();
    spdlog::set_level(spdlog::level::info);

    // create http server
    const std::string addr = "0.0.0.0";
    const int port = 6666;
    HttpServer http_server(addr, port, args);
    http_server.Start();


    // const std::string addr = "0.0.0.0";
    // const int port = 7777;
    // SocketServer sock_server(addr, port);
    // sock_server.Start();
    // while (true) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(250));
    // }

    return 0;
}
