#include "MachineInfoCollector.h"
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>
#include "device_type.h"
#include "device.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <random>
#include <cstdlib>
#include <string>

// 将 const char* 硬编码改为全局变量
// 默认值设为 127.0.0.1，方便本地测试。生产环境建议通过参数覆盖。
std::string g_gateway_ip = "127.0.0.1";
int g_gateway_port = 6666;

const int kAgentPort = 8000;

using json = nlohmann::json;
using namespace httplib;
using namespace std::chrono_literals;

// 全局原子变量标记是否运行（用于线程安全退出）
std::atomic<bool> g_is_running(true);

// 随机数生成器（用于带宽波动，范围调整为50-500Mbps）
static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_real_distribution<> bandwidth_dist(50.0, 500.0); // 50-500Mbps的波动范围

static std::string BuildResult(const std::string &status, const json &v) {
    json j;
    j["status"] = status;
    j["result"] = v;
    return j.dump();
}

static std::string BuildSuccess(const json &v) { return BuildResult("success", v); }

static std::string BuildFailed(const json &v) { return BuildResult("failed", v); }

static bool RegisterNode(MachineInfoCollector &collector) {
    try {
        // --- 修改点 2: 使用全局变量 g_gateway_ip 和 g_gateway_port ---
        Client client(g_gateway_ip.c_str(), g_gateway_port);

        json j = {
                {"type",       AGENT_DEVICE_TYPE},
                {"global_id",  collector.GetGlobalId()},
                {"ip_address", collector.GetIp()},
                {"agent_port", kAgentPort},
        };

        Result result = client.Post("/register_node", j.dump(), "application/json");
        if (!result || result->status != OK_200) {
            spdlog::error("Failed to register node: {}", httplib::to_string(result.error()));
            return false;
        }

        spdlog::info("Node registered successfully");
        return true;
    } catch (const std::exception &e) {
        spdlog::error("Failed to register node: {}", e.what());
        return false;
    }
}

static bool DisconnectNode(MachineInfoCollector &collector) {
    try {
        // --- 修改点 3: 使用全局变量 ---
        Client client(g_gateway_ip.c_str(), g_gateway_port);

        json j = {
                {"type",       AGENT_DEVICE_TYPE},
                {"global_id",  collector.GetGlobalId()},
                {"ip_address", collector.GetIp()},
                {"agent_port", kAgentPort},
        };

        Result result = client.Post("/unregister_node", j.dump(), "application/json");
        if (!result || result->status != OK_200) {
            spdlog::error("Failed to disconnect node: {}", httplib::to_string(result.error()));
            return false;
        }

        spdlog::info("Node disconnected successfully");
        return true;
    } catch (const std::exception &e) {
        spdlog::error("Failed to disconnect node: {}", e.what());
        return false;
    }
}

// 后台定时线程：处理自动断开和重连（支持禁用自动断开）
static void AutoConnectThread(MachineInfoCollector &collector, int disconnect_sec, int reconnect_sec) {
    // 如果disconnect_sec <= 0，直接禁用自动断开重连功能
    if (disconnect_sec <= 0) {
        spdlog::info("Auto-disconnect is disabled (disconnect time <= 0)");
        // 线程进入等待状态，直到程序退出
        while (g_is_running) {
            std::this_thread::sleep_for(1s);
        }
        return;
    }

    // 正常执行自动断开重连逻辑
    while (g_is_running) {
        // 步骤1：等待disconnect_sec秒后断开连接
        spdlog::info("Waiting {}s to disconnect...", disconnect_sec);
        for (int i = 0; i < disconnect_sec && g_is_running; ++i) {
            std::this_thread::sleep_for(1s); // 每秒检查一次是否需要退出
        }
        if (!g_is_running) break;

        // 发送断开连接请求
        DisconnectNode(collector);

        // 步骤2：等待reconnect_sec秒后重新注册
        spdlog::info("Waiting {}s to reconnect...", reconnect_sec);
        for (int i = 0; i < reconnect_sec && g_is_running; ++i) {
            std::this_thread::sleep_for(1s);
        }
        if (!g_is_running) break;

        // 重新注册
        RegisterNode(collector);
    }
}

// 打印帮助信息
static void PrintHelp(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --master-ip <ip>         Set Master/Gateway IP (env: MASTER_IP, default: 127.0.0.1)\n"
              << "  --master-port <port>     Set Master/Gateway Port (env: MASTER_PORT, default: 6666)\n"
              << "  --disconnect <seconds>   Set auto-disconnect time (default: 30s, <=0 to disable)\n"
              << "  --reconnect <seconds>    Set auto-reconnect time (default: 20s)\n"
              << "  --bandwidth-fluctuate    Enable network bandwidth fluctuation simulation (50-500Mbps)\n"
              << "  --help                   Show this help message\n" << std::endl;
}

int main(int argc, char* argv[]) {
    // --- 修改点 4: 优先读取环境变量 ---
    const char* env_ip = std::getenv("MASTER_IP");
    if (env_ip) {
        g_gateway_ip = env_ip;
    }
    const char* env_port = std::getenv("MASTER_PORT");
    if (env_port) {
        try {
            g_gateway_port = std::stoi(env_port);
        } catch (...) {
            spdlog::warn("Invalid MASTER_PORT env var, using default: {}", g_gateway_port);
        }
    }

    // 默认参数设置
    int disconnect_sec = 30;    // 默认断连时间30秒
    int reconnect_sec = 20;     // 默认重连时间20秒
    bool bandwidth_fluctuate = false;  // 默认不开启带宽波动

    // 解析命令行参数（允许disconnect_sec <=0）
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--disconnect" && i + 1 < argc) {
            try {
                disconnect_sec = std::stoi(argv[++i]);
                if (disconnect_sec < 0) {
                    spdlog::info("Auto-disconnect will be disabled (negative value provided)");
                }
            } catch (const std::exception& e) {
                spdlog::error("Invalid disconnect time: {}", e.what());
                PrintHelp(argv[0]);
                return 1;
            }
        } else if (arg == "--reconnect" && i + 1 < argc) {
            try {
                reconnect_sec = std::stoi(argv[++i]);
                if (reconnect_sec <= 0) throw std::invalid_argument("must be positive");
            } catch (const std::exception& e) {
                spdlog::error("Invalid reconnect time: {}", e.what());
                PrintHelp(argv[0]);
                return 1;
            }
        } else if (arg == "--bandwidth-fluctuate") {
            bandwidth_fluctuate = true;
            spdlog::info("Bandwidth fluctuation enabled (range: 50-500Mbps)");
        }

        else if (arg == "--master-ip" && i + 1 < argc) {
            g_gateway_ip = argv[++i];
        } else if (arg == "--master-port" && i + 1 < argc) {
            try {
                g_gateway_port = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                spdlog::error("Invalid master port: {}", e.what());
                return 1;
            }
        }
        else if (arg == "--help") {
            PrintHelp(argv[0]);
            return 0;
        } else {
            spdlog::error("Unknown argument: {}", arg);
            PrintHelp(argv[0]);
            return 1;
        }
    }

    // 打印参数配置
    spdlog::info("===== Agent Configuration =====");
    spdlog::info("Master IP: {}", g_gateway_ip);
    spdlog::info("Master Port: {}", g_gateway_port);
    if (disconnect_sec <= 0) {
        spdlog::info("Auto-disconnect: Disabled");
    } else {
        spdlog::info("Auto-disconnect time: {}s", disconnect_sec);
    }
    spdlog::info("Auto-reconnect time: {}s", reconnect_sec);
    spdlog::info("Bandwidth fluctuation: {}", (bandwidth_fluctuate ? "Enabled (50-500Mbps)" : "Disabled"));
    spdlog::info("===============================\n");

    // 使用动态地址初始化 MachineInfoCollector
    MachineInfoCollector collector(g_gateway_ip, g_gateway_port);
    httplib::Server server;

    // 初始注册节点
    if (!RegisterNode(collector)) {
        spdlog::error("Initial registration failed. Exiting.");
        return 1;
    }

    // 启动后台自动断开重连线程
    std::thread auto_connect_thread(AutoConnectThread, std::ref(collector), disconnect_sec, reconnect_sec);

    // 异常处理
    server.set_exception_handler([](const auto &req, auto &res, std::exception_ptr ep) {
        res.status = httplib::OK_200;
        std::string msg;
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception &e) {
            msg = e.what();
        } catch (...) {
            msg = "unknown exception";
        }
        spdlog::error("exception: {}", msg);
        res.set_content(BuildFailed(msg), "application/json");
    });

    // 设备信息接口（附带打印）
    server.Get("/usage/device_info", [&collector, bandwidth_fluctuate, disconnect_sec, reconnect_sec](const httplib::Request &, httplib::Response &res) {
        DeviceStatus dev_info;
        dev_info.disconnectTime = disconnect_sec;
        dev_info.reconnectTime = reconnect_sec;
        dev_info.timeWindow = 5;
        dev_info.cpu_used = collector.GetCpuUsage();
        dev_info.mem_used = collector.GetMemoryUsage();
        dev_info.xpu_used = collector.GetNpuUsage();
        dev_info.net_latency = collector.GetNetLatency(); // ms

        // 处理带宽波动
        double bandwidth;
        if (bandwidth_fluctuate) {
            bandwidth = bandwidth_dist(gen);
        } else {
            bandwidth = collector.GetNetBandwidth();
        }
        dev_info.net_bandwidth = bandwidth;

        // 打印设备信息到终端
        spdlog::info("\n===== 设备信息 =====");
        spdlog::info("CPU 使用率: {}%", dev_info.cpu_used * 100);
        spdlog::info("内存使用率: {}%", dev_info.mem_used * 100);
        spdlog::info("NPU 使用率: {}%", dev_info.xpu_used * 100);
        spdlog::info("网络延迟: {} ms", dev_info.net_latency);
        spdlog::info("网络带宽: {} Mbps", dev_info.net_bandwidth);
        spdlog::info("断开等待时间: {}", (dev_info.disconnectTime <= 0 ? "Disabled" : std::to_string(dev_info.disconnectTime) + " s"));
        spdlog::info("重连等待时间: {} s", dev_info.reconnectTime);
        spdlog::info("====================\n");

        // 构建响应
        std::string result = BuildSuccess(dev_info.to_json());
        res.set_content(result, "application/json");
    });

    // 启动服务器
    spdlog::info("Starting docker scheduler agent on port {}", kAgentPort);
    if (!server.listen("0.0.0.0", kAgentPort)) {
        spdlog::error("Failed to start server");
        g_is_running = false; // 通知线程退出
        auto_connect_thread.join(); // 等待线程结束
        return 1;
    }

    // 服务器退出时，通知线程并等待结束
    g_is_running = false;
    auto_connect_thread.join();

    return 0;
}
