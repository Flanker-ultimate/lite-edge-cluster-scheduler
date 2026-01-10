#ifndef DOCKER_SCHEDULER_AGENT_MACHINEINFOCOLLECTORBASE_H
#define DOCKER_SCHEDULER_AGENT_MACHINEINFOCOLLECTORBASE_H

#include <thread>
#include <mutex>
#include <string>
#include <queue>
#include <atomic>

const static size_t kCpuUsageQueueSize = 5;
const static double DISCONNECTTIME = 30.0;
const static double RECONNECTTIME = 10.0;

struct CpuUsageInfo {
    uint64_t user;
    uint64_t system;
    uint64_t idle;
};

class MachineInfoCollectorBase {
public:
    MachineInfoCollectorBase(std::string gatewayIp, int gatewayPort)
            : gatewayIp(std::move(gatewayIp)), gatewayPort(gatewayPort) {
        for (size_t i = 0; i < kCpuUsageQueueSize; i++) {
            cpuUsageQueue.push_back(0.0);
        }
        StartCollect();
    }
    virtual ~MachineInfoCollectorBase();

    double GetCpuUsage();

    double GetMemoryUsage();

    double GetNetLatency();

    double GetNetBandwidth();

    std::string GetIp();

    std::string GetGlobalId();

private:
    std::thread collectorThread;
    std::mutex collectorMutex;
    std::atomic<bool> stop_{false};

    CpuUsageInfo prevCpuUsage{};
    CpuUsageInfo currCpuUsage{};
    std::deque<double> cpuUsageQueue;

    const std::string gatewayIp;
    const int gatewayPort{};
    double netLatency{};
    double netBandwidth{};


    void StartCollect();
    void StopCollect();

    void CollectThread();

    void CollectCpuUsage();

    void CollectNetLatency();

    void CollectNetBandwidth();
};

#endif // DOCKER_SCHEDULER_AGENT_MACHINEINFOCOLLECTORBASE_H
