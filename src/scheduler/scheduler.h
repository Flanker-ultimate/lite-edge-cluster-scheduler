#ifndef DOCKER_SCHEDULER_H
#define DOCKER_SCHEDULER_H

#include <string>
#include <map>
#include <vector>
#include <list>
#include <queue>
#include <deque>
#include <unordered_map>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <boost/uuid/uuid_hash.hpp>
#include "device.h"
#include <optional>
#include "spdlog/spdlog.h"
#include "TimeRecorder.h"
//#include <cpu_provider_factory.h>
//#include <provider_options.h>
//#include <onnxruntime_cxx_api.h>
using namespace std;
using json = nlohmann::json;
#pragma once

enum class TaskStatus {
    PENDING,
    RUNNING
};

enum class ScheduleStrategy {
    LOAD_BASED,
    ROUND_ROBIN
};

struct ImageTask {
    std::string task_id;   // unique identifier, prefer filename
    std::string file_path; // absolute path on master disk
    std::string client_ip; // source ip for slave to report
    TaskType task_type{TaskType::Unknown};
    ScheduleStrategy schedule_strategy{ScheduleStrategy::LOAD_BASED};
    int retry_count{0};
    TaskStatus status{TaskStatus::PENDING};
};

struct StaticInfoItem {
    ImageInfo imageInfo;
    TaskOverhead taskOverhead;
    StaticInfoItem(){};
    StaticInfoItem(const json &device) {
        imageInfo.container_name = device["imageInfo"]["container_name"];
        imageInfo.image = device["imageInfo"]["image"];
        imageInfo.cmds = device["imageInfo"]["cmds"].get<std::vector<std::string> >();
        imageInfo.args = device["imageInfo"]["args"].get<std::vector<std::string> >();
        imageInfo.host_config_privileged = device["imageInfo"]["host_config_privileged"];
        imageInfo.env = device["imageInfo"]["env"].get<std::vector<std::string> >();
        imageInfo.host_config_binds = device["imageInfo"]["host_config_binds"].get<std::vector<std::string> >();
        imageInfo.devices = device["imageInfo"]["devices"].get<std::vector<std::string> >();
        imageInfo.host_ip = device["imageInfo"]["host_ip"];
        imageInfo.host_port = device["imageInfo"]["host_port"];
        imageInfo.container_port = device["imageInfo"]["container_port"];
        imageInfo.has_tty = device["imageInfo"]["has_tty"];

        taskOverhead.proc_time = device["taskOverhead"]["proc_time"];
        taskOverhead.mem_usage = device["taskOverhead"]["mem_usage"];
        taskOverhead.cpu_usage = device["taskOverhead"]["cpu_usage"];
        taskOverhead.xpu_usage = device["taskOverhead"]["xpu_usage"];
    }
};


class TaskQueueManager {
public:
    void PushPending(const ImageTask &task, bool high_priority);
    std::optional<ImageTask> PopPending();
    void RecoverTasks(const DeviceID &device_id);
    bool AddRunningTask(const DeviceID &device_id, const ImageTask &task);
    bool CompleteTask(const std::string &task_id);
    void MoveToFailed(const ImageTask &task);

private:
    std::deque<ImageTask> pending_queue_;
    std::unordered_map<DeviceID, std::list<ImageTask>> running_index_;
    std::list<ImageTask> failed_history_;
    std::mutex mutex_;
    std::condition_variable pending_cv_;
};

class Docker_scheduler {
private:
    static std::map<TaskType, std::map<DeviceType, StaticInfoItem> > static_info; // static task info

    static std::shared_mutex devs_mutex; //
    static std::map<DeviceID, Device> device_static_info; // static device info

    static std::map<DeviceID, DeviceStatus> device_status; // dynamic device info

    // static std::shared_mutex td_map_mutex_; // Thread-safe mutex for TDMap
    static std::map<TaskType, std::map<DeviceID, DevSrvInfos> > tdMap;

    //  dynamic device info unorder_map becaues of uuid_t cant compare for the need of map

    int scheduling_trget; // current scheduling_target
    static TaskQueueManager task_queue_manager_;
    static std::once_flag scheduler_loop_once_flag_;

    static Device selectDeviceByLoad(const std::vector<DeviceID>& devIds);

    //onnx
//    static Ort::Env env;
//    static Ort::Session* onnx_session;  // 使用指针避免初始化时构造
    static bool is_model_loaded;  // 标记模型是否已加载
    static size_t rr_index; // 轮询用的索引
public:
    Docker_scheduler();

    /// @brief read profiling result from knowledge_file and initilize
    /// @param knowledge_file name of the file
    explicit Docker_scheduler(string knowledge_file);

    // func of device_static_info
    static vector<TaskType> getTaskTypesByDeviceType(DeviceType devType);

    static void RemoveDevice(DeviceID global_id);

    /// @brief Add a new node in the cluster with its type, ip address, and agent port
    /// @param node_type device type, RK3588, ATLAS, or ORIN
    /// @param IP the IP address of the new node
    /// @param port the agent port of the new node
    /// @return the global id of the new node
    static int RegisNode(const Device &device);


    static void display_dev();

    static void display_devinfo();

    // Methods to access device status information for logging
    static std::map<DeviceID, DeviceStatus>& getDeviceStatus() { return device_status; }
    static std::shared_mutex& getDeviceMutex() { return devs_mutex; }

    /// @brief init scheduler
    /// @param filepath profiling file path
    static void init(string filepath);

    // read file to static_Info
    static void loadStaticInfo(std::string filepath);
    // get static_info
    static std::map<TaskType, std::map<DeviceType, StaticInfoItem>> getStaticInfo() ;

    static ImageInfo getImage(TaskType taskType, DeviceType devType);

    /// @brief Thread-safe method to remove a device
    /// @param global_id The global ID of the device to be removed
    static void RemoveDevice(int global_id);


    static bool HotStartAllNodeByTType(TaskType ttype);

    static void startDeviceInfoCollection();

    /// @brief route a srvinfo for a quest with a specific task type
    /// @param TaskType ttype
    /// @return Selected SrvInfo
    static std::optional<SrvInfo> getOrCrtSrvByTType(TaskType ttype);

    // create a new  container on a specific device
    static std::optional<SrvInfo> createContainerByTType(TaskType ttype, const Device &dev);

    /// @brief select a dev when creating a new container or deal a quest
    static Device getTgtDevByTtype(TaskType ttype);

    static Device getTgtDevByTtypeAndDevIds(TaskType ttype);

    static Device getTgtDevByTtypeAndDevIds(TaskType ttype, vector<DeviceID> devIds);


    /// @brief remove inactive container
    static void inactiveTimeCallback(TaskType ttype, Device dev, string container_id);

    /// @brief Get target device ID for new coming task with type Ttype
    /// @param Ttype the type of target task 
    /// @return target device
    static Device Schedule(TaskType Ttype);
    static Device Model_predict(TaskType Ttype);
    static Device Pic_Schedule(TaskType Ttype);
    // 模型加载函数
    static void loadModel(const std::string& model_path);
    static int encodeTaskType(TaskType Ttype);
    static int encodePlatform(DeviceType dtype);

    static Device RoundRobin_Schedule(TaskType Ttype);

    static bool Disconnect_device(Device device);
    static void StartSchedulerLoop();
    static void SchedulerLoop();
    static void SubmitTask(const ImageTask &task, bool high_priority = false);
    static bool CompleteTask(const std::string &task_id);
    void display_devstatus(DeviceID dev_id){
        DeviceStatus status = device_status[dev_id];
        status.show();
    }

    void updateStatus(DeviceID id,DeviceStatus status){
        device_status[id].cpu_used+=status.cpu_used;
        device_status[id].mem_used+=status.mem_used;
        device_status[id].xpu_used+=status.xpu_used;
    }
    void regissrv(DeviceID id,TaskType ttype){
        if(tdMap[ttype][id].dev_srv_info_status == NoExist){
            tdMap[ttype][id].dev_srv_info_status = Running;
        }
    }

    static TaskQueueManager& GetTaskQueueManager() { return task_queue_manager_; }


};

#endif
