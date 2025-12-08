#include "MachineInfoCollector.h"
#include <stdexcept>

double MachineInfoCollector::GetNpuUsage() {
    throw std::logic_error("Cannot get NPU usage on unknown architecture");
}
