#include "MachineInfoCollector.h"
#include <dsmi_common_interface.h>

// reference:
// https://support.huawei.com/enterprise/zh/doc/EDOC1100288849/7729981e

double MachineInfoCollector::GetNpuUsage() {
    unsigned int rate;
    int ret = dsmi_get_device_utilization_rate(0, /* device_type=NPU */ 2, &rate);
    if (ret != 0) {
        throw std::runtime_error("Failed to get NPU utilization rate");
    }

    return (double) rate / 100;
}
