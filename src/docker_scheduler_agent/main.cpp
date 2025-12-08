#include "MachineInfoCollector.h"
#include <httplib.h>
#include <iostream>
#include <nlohmann/json.hpp>
#include "device_type.h"
#include "device.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <random>
#include <cstdlib>

const char *kGatewayIp = "10.134.74.155";
const int kGatewayPort = 6666;
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
        Client client(kGatewayIp, kGatewayPort);

        json j = {
                {"type",       AGENT_DEVICE_TYPE},
                {"global_id",  collector.GetGlobalId()},
                {"ip_address", collector.GetIp()},
                {"agent_port", kAgentPort},
        };

        Result result = client.Post("/register_node", j.dump(), "application/json");
        if (!result || result->status != OK_200) {
            std::cerr << "Failed to register node: " << result.error() << std::endl;
            return false;
        }

        std::cout << "Node registered successfully" << std::endl;
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Failed to register node: " << e.what() << std::endl;
        return false;
    }
}

static bool DisconnectNode(MachineInfoCollector &collector) {
    try {
        Client client(kGatewayIp, kGatewayPort);

        json j = {
                {"type",       AGENT_DEVICE_TYPE},
                {"global_id",  collector.GetGlobalId()},
                {"ip_address", collector.GetIp()},
                {"agent_port", kAgentPort},
        };

        Result result = client.Post("/unregister_node", j.dump(), "application/json");
        if (!result || result->status != OK_200) {
            std::cerr << "Failed to disconnect node: " << result.error() << std::endl;
            return false;
        }

        std::cout << "Node disconnected successfully" << std::endl;
        return true;
    } catch (const std::exception &e) {
        std::cerr << "Failed to disconnect node: " << e.what() << std::endl;
        return false;
    }
}

// 后台定时线程：处理自动断开和重连（支持禁用自动断开）
static void AutoConnectThread(MachineInfoCollector &collector, int disconnect_sec, int reconnect_sec) {
    // 如果disconnect_sec <= 0，直接禁用自动断开重连功能
    if (disconnect_sec <= 0) {
        std::cout << "Auto-disconnect is disabled (disconnect time <= 0)" << std::endl;
        // 线程进入等待状态，直到程序退出
        while (g_is_running) {
            std::this_thread::sleep_for(1s);
        }
        return;
    }

    // 正常执行自动断开重连逻辑
    while (g_is_running) {
        // 步骤1：等待disconnect_sec秒后断开连接
        std::cout << "Waiting " << disconnect_sec << "s to disconnect..." << std::endl;
        for (int i = 0; i < disconnect_sec && g_is_running; ++i) {
            std::this_thread::sleep_for(1s); // 每秒检查一次是否需要退出
        }
        if (!g_is_running) break;

        // 发送断开连接请求
        DisconnectNode(collector);

        // 步骤2：等待reconnect_sec秒后重新注册
        std::cout << "Waiting " << reconnect_sec << "s to reconnect..." << std::endl;
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
              << "  --disconnect <seconds>   Set auto-disconnect time (default: 30s, <=0 to disable)\n"
              << "  --reconnect <seconds>    Set auto-reconnect time (default: 20s)\n"
              << "  --bandwidth-fluctuate    Enable network bandwidth fluctuation simulation (50-500Mbps)\n"  // 更新帮助信息
              << "  --help                   Show this help message\n" << std::endl;
}

int main(int argc, char* argv[]) {
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
                // 允许disconnect_sec <=0（禁用自动断开），不再抛出异常
                if (disconnect_sec < 0) {
                    std::cout << "Auto-disconnect will be disabled (negative value provided)" << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Invalid disconnect time: " << e.what() << std::endl;
                PrintHelp(argv[0]);
                return 1;
            }
        } else if (arg == "--reconnect" && i + 1 < argc) {
            try {
                reconnect_sec = std::stoi(argv[++i]);
                if (reconnect_sec <= 0) throw std::invalid_argument("must be positive");
            } catch (const std::exception& e) {
                std::cerr << "Invalid reconnect time: " << e.what() << std::endl;
                PrintHelp(argv[0]);
                return 1;
            }
        } else if (arg == "--bandwidth-fluctuate") {
            bandwidth_fluctuate = true;
            std::cout << "Bandwidth fluctuation enabled (range: 50-500Mbps)" << std::endl;  // 启动时提示波动范围
        } else if (arg == "--help") {
            PrintHelp(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            PrintHelp(argv[0]);
            return 1;
        }
    }

    // 打印参数配置（明确显示是否禁用自动断开）
    std::cout << "===== Agent Configuration =====" << std::endl;
    if (disconnect_sec <= 0) {
        std::cout << "Auto-disconnect: Disabled" << std::endl;
    } else {
        std::cout << "Auto-disconnect time: " << disconnect_sec << "s" << std::endl;
    }
    std::cout << "Auto-reconnect time: " << reconnect_sec << "s" << std::endl;
    std::cout << "Bandwidth fluctuation: " << (bandwidth_fluctuate ? "Enabled (50-500Mbps)" : "Disabled") << std::endl;  // 显示波动范围
    std::cout << "===============================\n" << std::endl;

    MachineInfoCollector collector(kGatewayIp, kGatewayPort);
    httplib::Server server;

    // 初始注册节点
    if (!RegisterNode(collector)) {
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
        std::cerr << "exception: " << msg << std::endl;
        res.set_content(BuildFailed(msg), "application/json");
    });

    // 设备信息接口（附带打印）
    server.Get("/usage/device_info", [&collector, bandwidth_fluctuate, disconnect_sec, reconnect_sec](const httplib::Request &, httplib::Response &res) {
        DeviceStatus dev_info;
        dev_info.disconnectTime = disconnect_sec;  // 使用参数值（可能为<=0表示禁用）
        dev_info.reconnectTime = reconnect_sec;    // 使用参数值
        dev_info.timeWindow = 5;
        dev_info.cpu_used = collector.GetCpuUsage();
        dev_info.mem_used = collector.GetMemoryUsage();
        dev_info.xpu_used = collector.GetNpuUsage();
        dev_info.net_latency = collector.GetNetLatency(); // ms

        // 处理带宽波动（直接生成50-500Mbps的随机值）
        double bandwidth;
        if (bandwidth_fluctuate) {
            bandwidth = bandwidth_dist(gen);  // 应用50-500Mbps的随机波动
        } else {
            bandwidth = collector.GetNetBandwidth(); // 原始带宽值
        }
        dev_info.net_bandwidth = bandwidth;

        // 打印设备信息到终端
        std::cout << "\n===== 设备信息 =====" << std::endl;
        std::cout << "CPU 使用率: " << dev_info.cpu_used * 100 << "%" << std::endl;
        std::cout << "内存使用率: " << dev_info.mem_used * 100 << "%" << std::endl;
        std::cout << "NPU 使用率: " << dev_info.xpu_used * 100 << "%" << std::endl;
        std::cout << "网络延迟: " << dev_info.net_latency << " ms" << std::endl;
        std::cout << "网络带宽: " << dev_info.net_bandwidth << " Mbps" << std::endl;
        std::cout << "断开等待时间: " << (dev_info.disconnectTime <= 0 ? "Disabled" : std::to_string(dev_info.disconnectTime) + " s") << std::endl;
        std::cout << "重连等待时间: " << dev_info.reconnectTime << " s" << std::endl;
        std::cout << "====================\n" << std::endl;

        // 构建响应
        std::string result = BuildSuccess(dev_info.to_json());
        res.set_content(result, "application/json");
    });

    // 启动服务器
    std::cout << "Starting docker scheduler agent on port " << kAgentPort << std::endl;
    if (!server.listen("0.0.0.0", kAgentPort)) {
        std::cerr << "Failed to start server" << std::endl;
        g_is_running = false; // 通知线程退出
        auto_connect_thread.join(); // 等待线程结束
        return 1;
    }

    // 服务器退出时，通知线程并等待结束
    g_is_running = false;
    auto_connect_thread.join();

    return 0;
}
