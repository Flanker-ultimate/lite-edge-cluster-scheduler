#include "MachineInfoCollectorBase.h"
#include <fstream>
#include <spdlog/spdlog.h>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>

const char *kConfigFilePath = ".agent_config.json";

double MachineInfoCollectorBase::GetCpuUsage() {
    std::lock_guard lock(collectorMutex);

    double sum = std::accumulate(cpuUsageQueue.begin(), cpuUsageQueue.end(), 0.0);
    double avg = sum / cpuUsageQueue.size();
    return avg;
}

double MachineInfoCollectorBase::GetMemoryUsage() {
    std::ifstream memFile("/proc/meminfo");
    if (!memFile.is_open()) {
        throw std::runtime_error("Failed to open /proc/meminfo");
    }

    unsigned long totalMem = 0;    // 总内存（单位：KB）
    unsigned long availableMem = 0;// 实际可用内存（单位：KB，含可回收缓存）
    std::string line, key;
    unsigned long value;
    std::string unit;

    // 遍历/proc/meminfo，提取MemTotal和MemAvailable字段
    while (std::getline(memFile, line)) {
        std::istringstream iss(line);
        if (iss >> key >> value >> unit) {
            if (key == "MemTotal:") {
                totalMem = value;
            } else if (key == "MemAvailable:") {
                availableMem = value;
            }
            // 两个字段都获取到后可提前退出，减少循环
            if (totalMem != 0 && availableMem != 0) {
                break;
            }
        }
    }

    // 校验数据有效性
    if (totalMem == 0) {
        throw std::runtime_error("Failed to parse MemTotal from /proc/meminfo");
    }
    if (availableMem == 0) {
        throw std::runtime_error("Failed to parse MemAvailable from /proc/meminfo");
    }

    // 计算内存使用率（0.0~1.0）：1 - 可用内存/总内存
    return 1.0 - static_cast<double>(availableMem) / totalMem;
}

double MachineInfoCollectorBase::GetNetLatency() {
    std::lock_guard lock(collectorMutex);
    return netLatency;
}

double MachineInfoCollectorBase::GetNetBandwidth() {
    std::lock_guard lock(collectorMutex);
    return netBandwidth;
}

void MachineInfoCollectorBase::StartCollect() {
    collectorThread = std::thread(&MachineInfoCollectorBase::CollectThread, this);
    collectorThread.detach();
}

void MachineInfoCollectorBase::CollectThread() {
    while (true) {
        try {
            CollectCpuUsage();
        } catch (const std::exception &e) {
            spdlog::error("Failed to collect CPU usage: {}", e.what());
        }

        try {
            CollectNetLatency();
        } catch (const std::exception &e) {
            spdlog::error("Failed to collect network latency: {}", e.what());
        }

        try {
            CollectNetBandwidth();
        } catch (const std::exception &e) {
            spdlog::error("Failed to collect network bandwidth: {}", e.what());
        }

        using namespace std::chrono_literals;
        std::this_thread::sleep_for(50ms);
    }
}

void MachineInfoCollectorBase::CollectCpuUsage() {
    CpuUsageInfo cpuUsage{};

    std::ifstream file("/proc/stat");
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open /proc/stat");
    }

    std::string name;
    file >> name;
    if (name != "cpu") {
        throw std::runtime_error("Failed to parse /proc/stat");
    }

    uint64_t nice;
    file >> cpuUsage.user >> nice >> cpuUsage.system >> cpuUsage.idle;

    // update
    {
        std::lock_guard lock(collectorMutex);
        prevCpuUsage = currCpuUsage;
        currCpuUsage = cpuUsage;

        uint64_t prevTotal = prevCpuUsage.user + prevCpuUsage.system + prevCpuUsage.idle;
        uint64_t currTotal = currCpuUsage.user + currCpuUsage.system + currCpuUsage.idle;

        uint64_t totalDiff = currTotal - prevTotal;
        uint64_t idleDiff = currCpuUsage.idle - prevCpuUsage.idle;

        if (totalDiff != 0) {
            double usage = 1.0 - (double) idleDiff / totalDiff;
            cpuUsageQueue.pop_front();
            cpuUsageQueue.push_back(usage);
        }
    }
}

void MachineInfoCollectorBase::CollectNetLatency() {
    // send Get request to gatewayIp
    httplib::Client client(gatewayIp, gatewayPort);

    auto start = std::chrono::high_resolution_clock::now();
    auto res = client.Get("/");
    auto end = std::chrono::high_resolution_clock::now();

    if (!res) {
        return;
    }

    // update
    {
        std::lock_guard lock(collectorMutex);
        // get latency in ms in double
        netLatency = std::chrono::duration<double, std::milli>(end - start).count();
    }
}

void MachineInfoCollectorBase::CollectNetBandwidth() {
    // Implement network bandwidth collection logic here
    // update now using random
    {
        std::lock_guard lock(collectorMutex);
        // get bandwidth in Mbps in double
        netBandwidth = 1000;
    }
}

std::string MachineInfoCollectorBase::GetIp() {
    struct ifaddrs *ifaddr, *ifa;
    char host[NI_MAXHOST];

    if (getifaddrs(&ifaddr) == -1) {
        throw std::runtime_error("Failed to get network interfaces");
    }

    std::string ip;
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;

        int family = ifa->ifa_addr->sa_family;
        if (family == AF_INET) {
            if (getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in),
                            host, NI_MAXHOST, nullptr, 0, NI_NUMERICHOST) == 0) {
                ip = host;
                if (ip != "127.0.0.1") {
                    break;
                }
            }
        }
    }

    freeifaddrs(ifaddr);

    if (ip.empty()) {
        throw std::runtime_error("Failed to get IP address");
    }

    return ip;
}

std::string MachineInfoCollectorBase::GetGlobalId() {
    const char *homeDir = getenv("HOME");
    if (homeDir == nullptr) {
        throw std::runtime_error("Could not find home directory.");
    }
    std::string configFilePath(homeDir);
    configFilePath += "/";
    configFilePath += kConfigFilePath;

    nlohmann::json jsonData;
    std::ifstream file(configFilePath);

    if (file.is_open()) {
        try {
            file >> jsonData;
            file.close();

            // Check if "global_id" exists in the JSON
            if (jsonData.contains("global_id")) {
                return jsonData["global_id"];
            }
        } catch (const std::exception &e) {
            // Handle any errors in reading/parsing JSON
            spdlog::error("Error reading or parsing JSON file: {}", e.what());
        }
    }

    // Generate a new UUID if file does not exist or global_id is not present
    boost::uuids::uuid uuid = boost::uuids::random_generator()();
    std::string uuid_str = boost::uuids::to_string(uuid);

    // Save the new UUID to the JSON file
    jsonData["global_id"] = uuid_str;
    std::ofstream outfile(configFilePath);
    if (outfile.is_open()) {
        outfile << jsonData.dump(4);  // Save JSON with indentation
        outfile.close();
    } else {
        spdlog::error("Error opening file for writing");
    }

    return uuid_str;
}
