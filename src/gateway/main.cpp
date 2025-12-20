#include "HttpServer.h"
#include "scheduler.h"

#include <spdlog/spdlog.h>

#include <string>

static Args parse_arguments(int argc, char *argv[]) {
    Args args;
    args.config_path = "./myapp";
    args.task_path = "./tasks";

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if ((arg == "--config" || arg == "-c") && i + 1 < argc) {
            args.config_path = argv[++i];
            continue;
        }
        if ((arg == "--task" || arg == "-t") && i + 1 < argc) {
            args.task_path = argv[++i];
            continue;
        }
        if (arg == "--keep-upload") {
            args.keep_upload = true;
            continue;
        }
    }
    return args;
}

int main(int argc, char *argv[]) {
    Args args = parse_arguments(argc, argv);
    spdlog::set_level(spdlog::level::info);
    spdlog::info("parse params config_path: {}, task_path: {}, keep_upload: {}",
                 args.config_path, args.task_path, args.keep_upload);

    Docker_scheduler::init(args.config_path + "/static_info.json");
    Docker_scheduler::startDeviceInfoCollection();

    const std::string addr = "0.0.0.0";
    const int port = 6666;
    HttpServer http_server(addr, port, args);
    http_server.Start();

    return 0;
}
