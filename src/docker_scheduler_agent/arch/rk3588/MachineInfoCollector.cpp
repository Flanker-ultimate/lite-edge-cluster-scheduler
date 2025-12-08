#include "MachineInfoCollector.h"
#include <fstream>
#include <iostream>

double MachineInfoCollector::GetNpuUsage() {
    std::ifstream file("/sys/kernel/debug/rknpu/load");
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open NPU load file");
    }

    // NPU load:  Core0:  0%, Core1:  0%, Core2:  0%,
    std::string token;
    int coreCount = 0;
    int coreLoad = 0;
    double totalLoad = 0.0;
    while (file >> token) {
        if (token.find("Core") != std::string::npos) {
            coreCount++;
            file >> coreLoad;
            totalLoad += (double)coreLoad / 100;
        }
    }

    if (coreCount == 0) {
        throw std::runtime_error("Failed to get NPU core count");
    }

    return totalLoad / coreCount;
}
