#include <gtest/gtest.h>
#include <spdlog/spdlog.h>
#include "scheduler.h"  // include Docker_scheduler

std::map<TaskType, std::string> taskTypeToString = {
        {YoloV5, "YoloV5"},
        {MobileNet, "MobileNet"},
        {Bert, "Bert"},
        {ResNet50, "ResNet50"},
        {deeplabv3, "deeplabv3"},
        {transcoding, "transcoding"},
        {decoding, "decoding"},
        {encoding, "encoding"},
        {Unknown, "Unknown"}
};
std::map<DeviceType, std::string> deviceTypeToString = {
        {RK3588, "RK3588"},
        {ATLAS_L, "ATLAS_L"},
        {ATLAS_H, "ATLAS_H"},
        {ORIN, "ORIN"}
};
void DisplayStaticInfo(const std::map<TaskType, std::map<DeviceType, StaticInfoItem>>& static_info) {
    for (const auto& task_pair : static_info) {
        TaskType task_type = task_pair.first;
        const auto& device_map = task_pair.second;

        // output TaskType
        spdlog::info("TaskType: {}", taskTypeToString[task_type]);

        for (const auto& device_pair : device_map) {
            DeviceType device_type = device_pair.first;
            const StaticInfoItem& info_item = device_pair.second;

            // output DeviceType and StaticInfoItem
            spdlog::info("  DeviceType: {}   CPU Usage: {}   NPU Usage: {}   Proc Time: {} ms   Mem Usage: {} MB",
                deviceTypeToString[device_type],
                info_item.taskOverhead.cpu_usage,
                info_item.taskOverhead.xpu_usage,
                info_item.taskOverhead.proc_time,
                info_item.taskOverhead.mem_usage);
        }
    }
}


// test read JSON file and put into map
TEST(DockerSchedulerTest, TestJsonToMap) {
    // json file location
    std::string test_file = "../../../config_files/static_info.json";

    // create Docker_scheduler and load json
    Docker_scheduler scheduler(test_file);
    // get static_info content
    DisplayStaticInfo(Docker_scheduler::getStaticInfo());
}



