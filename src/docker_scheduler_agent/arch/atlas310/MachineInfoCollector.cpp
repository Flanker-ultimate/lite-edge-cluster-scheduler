#include "MachineInfoCollector.h"
#include <dcmi_interface_api.h>
#include <vector>
#include <cstring>

double MachineInfoCollector::GetNpuUsage() {
    // 静态变量存储NPU设备列表及初始化状态，避免重复初始化
    static std::vector<std::pair<int, int>> npu_devices;
    static bool is_initialized = false;

    // 首次调用时初始化DCMI并扫描NPU设备
    if (!is_initialized) {
        // 初始化DCMI
        if (dcmi_init() == DCMI_OK) {
            // 获取卡列表
            int max_card_num = MAX_CARD_NUM;
            int actual_card_num = 0;
            std::vector<int> card_list(max_card_num);
            if (dcmi_get_card_list(&actual_card_num, card_list.data(), max_card_num) == DCMI_OK) {
                // 遍历每张卡查找NPU设备
                for (int i = 0; i < actual_card_num; ++i) {
                    int card_id = card_list[i];
                    int device_id_max = 0;
                    int mcu_id, cpu_id;
                    if (dcmi_get_device_id_in_card(card_id, &device_id_max, &mcu_id, &cpu_id) == DCMI_OK) {
                        for (int dev_id = 0; dev_id < device_id_max; ++dev_id) {
                            dcmi_unit_type dev_type;
                            if (dcmi_get_device_type(card_id, dev_id, &dev_type) == DCMI_OK && dev_type == NPU_TYPE) {
                                npu_devices.emplace_back(card_id, dev_id);
                            }
                        }
                    }
                }
            }
        }
        is_initialized = true;
    }

    // 无可用NPU设备返回0
    if (npu_devices.empty()) {
        return 0.0;
    }

    // 计算所有NPU设备的平均AI Core利用率
    double total_rate = 0.0;
    int valid_count = 0;
    const int input_type = DCMI_UTILIZATION_RATE_AICORE;

    for (const auto& device : npu_devices) {
        unsigned int rate = 0;
        if (device.first == 0 && dcmi_get_device_utilization_rate(device.first, device.second, input_type, &rate) == DCMI_OK) {
            // 限制利用率范围在0-100
            if (rate > 100) rate = 100;
            total_rate += rate;
            valid_count++;
            break;
        }
    }

    // 返回平均利用率（转换为0-1范围）
    return valid_count > 0 ? (total_rate / valid_count) / 100.0 : 0.0;
}
