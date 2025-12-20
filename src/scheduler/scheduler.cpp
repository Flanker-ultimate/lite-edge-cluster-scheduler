#include"scheduler.h"
#include <spdlog/spdlog.h>
#include <spdlog/fmt/bundled/ostream.h>
#include<fstream>
#include<thread>
#include<chrono>
#include <DockerClient.h>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <filesystem>
#include <algorithm>
std::map<TaskType, std::map<DeviceType, StaticInfoItem> > Docker_scheduler::static_info; // static task info
TaskQueueManager Docker_scheduler::task_queue_manager_;

std::shared_mutex Docker_scheduler::devs_mutex; //
std::map<DeviceID, Device> Docker_scheduler::device_static_info; // static device info

std::map<DeviceID, DeviceStatus> Docker_scheduler::device_status; // dynamic device info
std::map<DeviceID, std::vector<TaskType>> Docker_scheduler::device_active_services;

// std::shared_mutex Docker_scheduler::td_map_mutex_; // Thread-safe mutex for TDMap
std::map<TaskType, std::map<DeviceID, DevSrvInfos> > Docker_scheduler::tdMap;
std::once_flag Docker_scheduler::scheduler_loop_once_flag_;
// onnx
//Ort::Env Docker_scheduler::env(ORT_LOGGING_LEVEL_WARNING, "OnnxModel");
//Ort::Session* Docker_scheduler::onnx_session = nullptr;
bool Docker_scheduler::is_model_loaded = false;
size_t Docker_scheduler::rr_index = 0;
std::optional<ImageTask> TaskQueueManager::PopPending() {
    std::unique_lock<std::mutex> lock(mutex_);
    pending_cv_.wait(lock, [this]() { return !pending_queue_.empty(); });
    ImageTask task = pending_queue_.front();
    pending_queue_.pop_front();
    return task;
}

void TaskQueueManager::PushPending(const ImageTask &task, bool high_priority) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (high_priority) {
            pending_queue_.push_front(task);
        } else {
            pending_queue_.push_back(task);
        }
    }
    pending_cv_.notify_one();
}

bool TaskQueueManager::AddRunningTask(const DeviceID &device_id, const ImageTask &task) {
    std::lock_guard<std::mutex> lock(mutex_);
    ImageTask running_task = task;
    running_task.status = TaskStatus::RUNNING;
    running_index_[device_id].push_back(running_task);
    return true;
}

std::optional<ImageTask> TaskQueueManager::CompleteTaskAndGet(const std::string &reported_task_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const std::filesystem::path reported_path(reported_task_id);
    const std::string reported_stem = reported_path.stem().string();

    for (auto map_it = running_index_.begin(); map_it != running_index_.end(); ++map_it) {
        auto &task_list = map_it->second;
        for (auto it = task_list.begin(); it != task_list.end(); ++it) {
            const std::filesystem::path running_path(it->task_id);
            const bool id_match = (it->task_id == reported_task_id);
            const bool stem_match = (!reported_stem.empty() && running_path.stem().string() == reported_stem);
            if (id_match || stem_match) {
                ImageTask completed = *it;
                task_list.erase(it);
                return completed;
            }
        }
    }

    return std::nullopt;
}

bool TaskQueueManager::CompleteTask(const std::string &task_id) {
    return CompleteTaskAndGet(task_id).has_value();
}

void TaskQueueManager::RecoverTasks(const DeviceID &device_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = running_index_.find(device_id);
    if (it == running_index_.end()) {
        return;
    }
    auto &tasks = it->second;
    for (auto &task : tasks) {
        task.retry_count += 1;
        task.status = TaskStatus::PENDING;
        if (task.retry_count <= 3) {
            pending_queue_.push_front(task);
        } else {
            failed_history_.push_back(task);
        }
    }
    running_index_.erase(it);
    pending_cv_.notify_one();
}

void TaskQueueManager::MoveToFailed(const ImageTask &task) {
    std::lock_guard<std::mutex> lock(mutex_);
    failed_history_.push_back(task);
}

void Docker_scheduler::StartSchedulerLoop() {
    std::call_once(scheduler_loop_once_flag_, []() {
        std::thread(&Docker_scheduler::SchedulerLoop).detach();
    });
}

void Docker_scheduler::SubmitTask(const ImageTask &task, bool high_priority) {
    task_queue_manager_.PushPending(task, high_priority);
    StartSchedulerLoop();
}

bool Docker_scheduler::CompleteTask(const std::string &task_id) {
    return task_queue_manager_.CompleteTask(task_id);
}

void Docker_scheduler::SchedulerLoop() {
    const int max_retries = 3;
    while (true) {
        auto task_opt = task_queue_manager_.PopPending();
        if (!task_opt.has_value()) {
            continue;
        }
        ImageTask task = *task_opt;

        Device target_device;
        try {
            target_device = task.schedule_strategy == ScheduleStrategy::ROUND_ROBIN ? RoundRobin_Schedule(task.task_type) : Schedule(task.task_type);

            // Add device load logging
            {
                std::shared_lock<std::shared_mutex> lock(devs_mutex);
                auto status_it = device_status.find(target_device.global_id);
                if (status_it != device_status.end()) {
                    const auto& status = status_it->second;
                    spdlog::info("Task {} selected device {} [CPU: {:.2f}%, MEM: {:.2f}%, XPU: {:.2f}%, Bandwidth: {:.2f}Mbps, Latency: {}ms]",
                                 task.task_id, target_device.ip_address,
                                 status.cpu_used * 100, status.mem_used * 100, status.xpu_used * 100,
                                 status.net_bandwidth, static_cast<int>(status.net_latency * 1000));
                }
            }
        } catch (const std::exception &e) {
            spdlog::error("Schedule failed for task {}: {}", task.task_id, e.what());
            task.retry_count++;
            if (task.retry_count <= max_retries) {
                task_queue_manager_.PushPending(task, true);
            } else {
                task_queue_manager_.MoveToFailed(task);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        std::ifstream ifs(task.file_path, std::ios::binary);
        if (!ifs) {
            spdlog::error("Failed to open task file: {}", task.file_path);
            task.retry_count++;
            if (task.retry_count <= max_retries) {
                task_queue_manager_.PushPending(task, true);
            } else {
                task_queue_manager_.MoveToFailed(task);
            }
            continue;
        }
        std::ostringstream buffer;
        buffer << ifs.rdbuf();
        std::string image_data = buffer.str();

        nlohmann::json meta_json;
        meta_json["ip"] = task.client_ip;
        meta_json["file_name"] = task.task_id;
        // Avoid JSON string being serialized with quotes like "\"YoloV5\""
        meta_json["tasktype"] = task.task_type == TaskType::Unknown ? "Unknown" : nlohmann::json(task.task_type);
        std::string meta_str = meta_json.dump();

        try {
            httplib::Client cli(target_device.ip_address, 20810);
            httplib::MultipartFormDataItems form_items = {
                {"pic_file", image_data, task.task_id, "application/octet-stream"},
                {"pic_info", meta_str, "", "application/json"}
            };
            auto res = cli.Post("/recv_task", form_items);
            if (res && res->status == 200) {
                task_queue_manager_.AddRunningTask(target_device.global_id, task);
                spdlog::info("Task {} dispatched to device {}", task.task_id, target_device.ip_address);
            } else {
                spdlog::warn("Send task {} failed, status={}", task.task_id, res ? res->status : -1);
                task.retry_count++;
                if (task.retry_count <= max_retries) {
                    task_queue_manager_.PushPending(task, true);
                } else {
                    task_queue_manager_.MoveToFailed(task);
                }
            }
        } catch (const std::exception &e) {
            spdlog::error("Exception sending task {}: {}", task.task_id, e.what());
            task.retry_count++;
            if (task.retry_count <= max_retries) {
                task_queue_manager_.PushPending(task, true);
            } else {
                task_queue_manager_.MoveToFailed(task);
            }
        }
    }
}

Docker_scheduler::Docker_scheduler() {
}

Docker_scheduler::Docker_scheduler(string knowledge_file) {

    ifstream infile;
    infile.open(knowledge_file, ios::in);
    //reading profiling result from infile, write them into tasks_info
    if (!infile.is_open()) {
        spdlog::error("Failed to open file: {}", knowledge_file);
        return;
    }
    // Parse the JSON file into a json object
    json json_data;
    infile >> json_data;
    infile.close();

    // Loop through the tasks in the JSON
    for (const auto &task_entry: json_data.items()) {
        // Get TaskType from key
        string task_str = task_entry.key();
        TaskType task_type;
        if (task_str == "YoloV5") task_type = YoloV5;
        else if (task_str == "MobileNet") task_type = MobileNet;
        else if (task_str == "Bert") task_type = Bert;
        else if (task_str == "ResNet50") task_type = ResNet50;
        else if (task_str == "deeplabv3") task_type = deeplabv3;
        else if (task_str == "transcoding") task_type = transcoding;
        else if (task_str == "decoding") task_type = decoding;
        else if (task_str == "encoding") task_type = encoding;
        else task_type = Unknown;
        spdlog::info("task load:{}", task_str);
        // Loop through the devices for each task
        for (const auto &device_entry: task_entry.value().items()) {
            string device_str = device_entry.key();
            DeviceType device_type;
            if (device_str == "RK3588") device_type = RK3588;
            else if (device_str == "ATLAS_L") device_type = ATLAS_L;
            else if (device_str == "ATLAS_H") device_type = ATLAS_H;
            else if (device_str == "ORIN") device_type = ORIN;
            else continue;
            spdlog::info("device load:{}", device_str);
            // Check if the required fields are present in the JSON before creating StaticInfoItem
            const auto &device = device_entry.value();
            if (device.contains("imageInfo") && device.contains("taskOverhead")) {
                // Create StaticInfoItem for the device only if the required fields exist
                StaticInfoItem static_info_item(device);

                // Add the StaticInfoItem to the static_info map
                static_info[task_type][device_type] = static_info_item;
            } else {
                spdlog::error("Missing necessary fields for device: {}", device_str);
            }
        }
    }
    return;
}

vector<TaskType> Docker_scheduler::getTaskTypesByDeviceType(DeviceType devType) {
    vector<TaskType> res;
    for (auto [ttype,v]: static_info) {
        if (v.find(devType) != v.end()) {
            res.push_back(ttype);
        }
    }
    return res;
}


int Docker_scheduler::RegisNode(const Device &device) {
    // update devs
    device_static_info[device.global_id] = device;
    // update dev_status
    device_status[device.global_id] = DeviceStatus();
    device_active_services[device.global_id] = device.services;

    // update Tdmap all tasktype add new device
    // according to task_static_info, match supported tasktype and device
    vector<TaskType> supportTType = Docker_scheduler::getTaskTypesByDeviceType(device.type);
    for (auto k: supportTType) {
        // tdMap[k].emplace(std::piecewise_construct,
        //      std::forward_as_tuple(device.global_id),
        //       std::forward_as_tuple());

        // DevSrvInfos temp;
        tdMap[k].try_emplace(device.global_id); // value constructor se default
    }
    return 0;
}


void Docker_scheduler::display_dev() {
    for (auto [id,status]: device_status) {
        if(device_static_info.count(id) > 0) {
            //cout << "Device ID: " << id << endl;
            //cout << "Device IP: " << device_static_info[id].ip_address << endl;
            //cout << "Device Port: " << device_static_info[id].agent_port << endl;
            spdlog::info("Device Type: {}", device_static_info[id].type);
            spdlog::info("Device Status: ");
            status.show();
        }
    }
}


void Docker_scheduler::init(string filepath) {
    loadStaticInfo(filepath);
    StartSchedulerLoop();
}

ImageInfo Docker_scheduler::getImage(TaskType taskType, DeviceType devType) {
    // First, check if taskType exists
    if (static_info.count(taskType) > 0) {
        // taskType exists, now check if devType exists within taskType
        if (static_info[taskType].count(devType) > 0) {
            // Both taskType and devType exist in static_info
            // Now you can safely check imageInfo
            return static_info[taskType][devType].imageInfo;
        } else {
            throw("[taskType,devType] doesn't exist in static_info");
        }
    } else {
        // taskType doesn't exist in static_info
        throw("taskType doesn't exist in static_info");
    }
}

void Docker_scheduler::loadStaticInfo(string filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open file");
    }
    json j;
    file >> j;
    for (auto &task: j.items()) {
        TaskType taskType;
        from_json(task.key(), taskType);
        for (auto &device: task.value().items()) {
            DeviceType deviceType;
            from_json(device.key(), deviceType);
            static_info[taskType][deviceType] = StaticInfoItem(device.value());;
        }
    }
}

void Docker_scheduler::RemoveDevice(DeviceID global_id) {
    for (auto [ttype, v]: tdMap) {
        auto it = tdMap[ttype].find(global_id);
        tdMap[ttype].erase(it);
    }
    device_active_services.erase(global_id);
}

bool Docker_scheduler::HotStartAllNodeByTType(TaskType ttype) {
    int support_ttype_dev_nums = 0;
    int start_container_nums = 0;
    for(auto [deviceId, devSrvInfos] : tdMap[ttype]) {
        support_ttype_dev_nums++;
        Device dev = device_static_info[deviceId];
        std::optional<SrvInfo> srvInfo = createContainerByTType(ttype, dev);
        if(srvInfo == nullopt) {
            spdlog::error("HotStartAllNodeByTType createContainer failed, ip:{}", dev.ip_address);
        }else {
            start_container_nums++;
            spdlog::info("HotStartAllNodeByTType createContainer success, ip:{}", dev.ip_address);
        }
    }
    spdlog::info("-----------HotStartAllNodeByTType createContainer info------------\n  support_ttype_dev_nums:{}, start_container_nums：{}\n", support_ttype_dev_nums, start_container_nums);
    return true;
}

void Docker_scheduler::startDeviceInfoCollection() {
    std::thread([]() {
        int count = 0; // 用于每10次打印一次所有设备的负载
        while (true) {
            {
                std::unique_lock<std::shared_mutex> lock(devs_mutex);
                for (auto [k, dev]: device_static_info) {
                    // start new Thread to collect
                    httplib::Client cli(dev.ip_address, dev.agent_port);
                    httplib::Result res;
                    try {
                        res = cli.Get("/usage/device_info");
                        // update device staus
                        if (res != nullptr && res.error() == httplib::Error::Success) {
                            string restr = res->body.data();
                            json j = json::parse(restr);
                            string resp_status = j["status"];
                            if (resp_status != "success") {
                                spdlog::error("Failed to get device info, agent return filed,dev.ip_address:{}, dev.agent_port:{}",
                                        dev.ip_address, dev.agent_port);
                                continue;
                            }
                            DeviceStatus status;
                            status.from_json(j["result"]);

                            auto it = device_status.find(k);
                            if (it != device_status.end()) {
                                it->second = status;  // 更新已有设备的状态
                            }

                            // agent 可选上报当前已启动的服务列表（用于 scheduler 优先选择已启动服务的节点）
                            try {
                                if (j.contains("result") && j["result"].is_object() &&
                                    j["result"].contains("services") && j["result"]["services"].is_array()) {
                                    std::vector<TaskType> running;
                                    for (const auto &sv : j["result"]["services"]) {
                                        if (!sv.is_string()) continue;
                                        TaskType tt = StrToTaskType(sv.get<std::string>());
                                        if (tt != TaskType::Unknown) {
                                            running.push_back(tt);
                                        }
                                    }
                                    device_active_services[k] = running;
                                }
                            } catch (...) {
                            }
                        } else {
                            spdlog::error("Failed to get device info, dev.ip_address:{}, dev.agent_port:{}",
                                          dev.ip_address, dev.agent_port);
                            continue;
                        }
                    } catch (const std::exception &e) {
                        spdlog::error("collect info error: {}", e.what());
                        continue;
                    }
                }
            }

            // 每10次打印一次所有设备的负载信息
            if (++count % 10 == 0) {
                spdlog::info("=== Device Load Summary ===");
                for (const auto& [device_id, dev]: device_static_info) {
                    auto status_it = device_status.find(device_id);
                    if (status_it != device_status.end()) {
                        const auto& status = status_it->second;
                        spdlog::info("Device {} ({}): CPU: {:.2f}%. MEM: {:.2f}%, XPU: {:.2f}%, Bandwidth: {:.2f}Mbps, Latency: {}ms",
                                     dev.ip_address, dev.type,
                                     status.cpu_used * 100, status.mem_used * 100, status.xpu_used * 100,
                                     status.net_bandwidth, static_cast<int>(status.net_latency * 1000));
                    }
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }).detach();
}




// CallBack Function to delete inactive contianer
void Docker_scheduler::inactiveTimeCallback(TaskType ttype, Device dev, string container_id) {
    std::this_thread::sleep_for(std::chrono::seconds(2)); // sleep to wait questing toi finish to avoid the current quest failed
    string docker_version = GetDockerVersion(dev);
    DockerClient docker_client(dev.ip_address, 2375, docker_version);
    bool delete_volume = false;
    bool force = true;
    bool delete_link_container = false;
    // fisrt tag deleting
    tdMap[ttype][dev.global_id].dev_srv_info_status = Deleting;
    // second remove
    bool rst = docker_client.RemoveContainer(container_id, delete_volume, force, delete_link_container);
    if(rst) {
        spdlog::info("Remove Container Success,contianerid:{},TaskType:{} ,ip:{}", container_id,  to_string(nlohmann::json(ttype)), dev.ip_address);
    }else {
        spdlog::error("Remove Container failed,contianerid:{},TaskType:{},ip:{}", container_id,to_string(nlohmann::json(ttype)), dev.ip_address);
    }

    // final set noexist
    tdMap[ttype][dev.global_id].dev_srv_info_status = NoExist;
    // modify info

    spdlog::info("inactiveTimeCallback triggered the callback!");
}

std::optional<SrvInfo> Docker_scheduler::getOrCrtSrvByTType(TaskType ttype) {
    if (tdMap.find(ttype) == tdMap.end()) {
        spdlog::error("No available service nodes to support this task type:{}", to_string(nlohmann::json(ttype)));
        return std::nullopt;
    }
    // step 1: select target device
    TimeRecord<chrono::milliseconds> schedule_timer("schedule_select");
    schedule_timer.startRecord();
    Device tgt_dev = getTgtDevByTtypeAndDevIds(ttype);
    schedule_timer.endRecord();
    spdlog::info("scheduler selection cost_time:{}", schedule_timer.getDuration());
    schedule_timer.clearRecord();

    // TODO deal no Device from schedule
    // judege there are creating Sr v
    // create a new container
    switch (tdMap[ttype][tgt_dev.global_id].dev_srv_info_status) {
        case DevSrvInfoStatus::Creating:{
            int index = 10;
            while (tdMap[ttype][tgt_dev.global_id].dev_srv_info_status == Creating || index > 0) {
                index--;
                // wait until create complete or quest time_out
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (index <= 0 || tdMap[ttype][tgt_dev.global_id].dev_srv_info_status == NoExist) {
                spdlog::error("for Tasktype:{},devIp:{}, the dev is creating contianer but over 10 times try or creating failed",
                    to_string(nlohmann::json(ttype)), tgt_dev.ip_address);
                return nullopt;
            }
            // refresh timeCallBack when access
            tdMap[ttype][tgt_dev.global_id].timer_callback.refresh();
            spdlog::info("ttype:{}, ip:{} timeCallBack refresh, lastTime={}, intervalTime={}",  to_string(nlohmann::json(ttype)), tgt_dev.ip_address, tdMap[ttype][tgt_dev.global_id].timer_callback.getElapsedTime(),  tdMap[ttype][tgt_dev.global_id].timer_callback.getIntervalTime());
            return tdMap[ttype][tgt_dev.global_id].srv_infos[0];
        }
        case DevSrvInfoStatus::Running:
            // refresh timeCallBack when access
            tdMap[ttype][tgt_dev.global_id].timer_callback.refresh();
        spdlog::info("ttype:{}, ip:{} timeCallBack refresh, lastTime={}, intervalTime={}",  to_string(nlohmann::json(ttype)), tgt_dev.ip_address, tdMap[ttype][tgt_dev.global_id].timer_callback.getElapsedTime(),  tdMap[ttype][tgt_dev.global_id].timer_callback.getIntervalTime());
            return tdMap[ttype][tgt_dev.global_id].srv_infos[0];
        case DevSrvInfoStatus::NoExist:{
            std::optional<SrvInfo> srv_info = createContainerByTType(ttype, tgt_dev);
            return srv_info;
        }
        default:
            spdlog::error("unkonwn error,for Tasktype:{},,device_ip:{}, getorCrtSrvByType", to_string(nlohmann::json(ttype)), tgt_dev.ip_address);
            return nullopt;
    }
}

std::optional<SrvInfo> Docker_scheduler::createContainerByTType(TaskType ttype, const Device &dev) {
    DeviceType dtype = dev.type;
    StaticInfoItem static_info_item = static_info[ttype][dtype];
    ImageInfo image_info = static_info_item.imageInfo;
    CreateContainerParam cparam = CreateContainerParam(
        image_info.container_name,
        image_info.image,
        image_info.cmds,
        image_info.args,
        image_info.host_config_privileged,
        image_info.env,
        image_info.host_config_binds,
        image_info.devices,
        image_info.host_ip,
        image_info.host_port,
        image_info.container_port,
        image_info.has_tty,
        image_info.network_config
    );
    string docker_version = GetDockerVersion(dev);
    DockerClient docker_client(dev.ip_address, 2375, docker_version);
    // first set Creating tag
    tdMap[ttype][dev.global_id].dev_srv_info_status = Creating;
    // then invoke api
    string container_id = docker_client.CreateContainer(cparam);
    if (container_id.empty()) {
        tdMap[ttype][dev.global_id].dev_srv_info_status = NoExist;
        spdlog::error("docker_client.CreateContainer failed, para:{}", container_id, cparam.toString());
        return nullopt;
    }
    spdlog::info("docker_client.CreateContainer Success, para:{}, ret:{}", container_id, cparam.toString());

    bool start_res = docker_client.StartContainer(container_id);
    if (!start_res) {
        tdMap[ttype][dev.global_id].dev_srv_info_status = NoExist;
        spdlog::error("docker start container failed, container_id={}", container_id);
        return nullopt;
    }

    // final set running tag
    SrvInfo srv_info{"", dev.ip_address, static_info_item.imageInfo.host_port};
    tdMap[ttype][dev.global_id].srv_infos.push_back(srv_info);
    tdMap[ttype][dev.global_id].dev_srv_info_status = Running;

    // start a timeCallback to delete inactive container
    auto bind_inactiveTimeCallback = std::bind(inactiveTimeCallback, ttype, dev, container_id);
    tdMap[ttype][dev.global_id].timer_callback.set_interval(600);
    tdMap[ttype][dev.global_id].timer_callback.set_callback(bind_inactiveTimeCallback);
    tdMap[ttype][dev.global_id].timer_callback.set_once_flag(true);
    tdMap[ttype][dev.global_id].timer_callback.start();

    return srv_info;
}

Device Docker_scheduler::getTgtDevByTtype(TaskType ttype) {
    std::vector<DeviceID> deviceIDs;
    for (const auto &pair: tdMap[ttype]) {
        deviceIDs.push_back(pair.first); // pair.first 是 DeviceID
    }

    return getTgtDevByTtypeAndDevIds(ttype, deviceIDs);
}

Device Docker_scheduler::getTgtDevByTtypeAndDevIds(TaskType ttype) {
    std::vector<DeviceID> deviceIDs;
    for (const auto &pair: tdMap[ttype]) {
        deviceIDs.push_back(pair.first);
    }
    return getTgtDevByTtypeAndDevIds(ttype, deviceIDs);
}


Device Docker_scheduler::getTgtDevByTtypeAndDevIds(TaskType ttype, vector<DeviceID> devIds) {
    try {
        return selectDeviceByLoad(devIds);
    } catch (const std::exception& e) {
        spdlog::warn("Load-based scheduling failed for task {}: {}. Falling back to round robin.", static_cast<int>(ttype), e.what());
        if (!devIds.empty()) {
            auto fallback = device_static_info.find(devIds.front());
            if (fallback != device_static_info.end()) {
                return fallback->second;
            }
        }
        return RoundRobin_Schedule(ttype);
    }
}

Device Docker_scheduler::selectDeviceByLoad(const std::vector<DeviceID>& devIds) {
    if (devIds.empty()) {
        throw std::runtime_error("No candidate devices available for scheduling.");
    }
    std::shared_lock<std::shared_mutex> lock(devs_mutex);

    const double w_cpu = 0.3;
    const double w_mem = 0.1;
    const double w_xpu = 0.4;
    const double w_bandwidth = 1;
    const double w_net_latency = 1;

    DeviceID best_device{};
    double min_load = std::numeric_limits<double>::max();
    bool found = false;
    std::ostringstream device_logs_stream;
    bool first_log_item = true;

    for (const auto& device_id : devIds) {
        auto it = device_status.find(device_id);
        auto dev_it = device_static_info.find(device_id);
        if (it == device_status.end() || dev_it == device_static_info.end()) {
            continue;
        }
        const auto& status = it->second;
        double cpu_val = status.cpu_used;
        double mem_val = status.mem_used;
        double xpu_val = status.xpu_used;
        double bandwidth_val = status.net_bandwidth;
        double net_latency_val = status.net_latency;
        double load = w_cpu * cpu_val +
                      w_mem * mem_val +
                      w_xpu * xpu_val +
                      w_bandwidth * bandwidth_val +
                      w_net_latency * net_latency_val;

        if (!first_log_item) {
            device_logs_stream << " | ";
        }
        device_logs_stream << fmt::format("device {}: cpu_used={}, mem_used={}, xpu_used={}, bandwidth={}, latency={}, weighted_score={}",
                                          dev_it->second.ip_address, cpu_val, mem_val, xpu_val, bandwidth_val, net_latency_val, load);
        first_log_item = false;

        if (load < min_load) {
            min_load = load;
            best_device = device_id;
            found = true;
        }
    }

    if (!found) {
        throw std::runtime_error("No device statuses available for scheduling.");
    }

    const auto selected = device_static_info.at(best_device);
    spdlog::info("Schedule metrics summary: [{}]; Selected device: {} with weighted_score={}",
                 device_logs_stream.str(), selected.ip_address, min_load);
    return selected;
}


std::map<TaskType, std::map<DeviceType, StaticInfoItem> > Docker_scheduler::getStaticInfo() {
    return static_info;
}

Device Docker_scheduler::Schedule(TaskType Ttype) {
    std::vector<DeviceID> device_ids;
    {
        std::shared_lock<std::shared_mutex> lock(devs_mutex);
        if (Ttype != TaskType::Unknown) {
            for (const auto& [device_id, services] : device_active_services) {
                if (device_status.find(device_id) == device_status.end()) {
                    continue;
                }
                if (std::find(services.begin(), services.end(), Ttype) != services.end()) {
                    device_ids.push_back(device_id);
                }
            }
            if (device_ids.empty()) {
                auto it = tdMap.find(Ttype);
                if (it != tdMap.end()) {
                    device_ids.reserve(it->second.size());
                    for (const auto& [device_id, _] : it->second) {
                        if (device_status.find(device_id) != device_status.end()) {
                            device_ids.push_back(device_id);
                        }
                    }
                }
            }
        }
        if (device_ids.empty()) {
            device_ids.reserve(device_status.size());
            for (const auto& [device_id, _] : device_status) {
                device_ids.push_back(device_id);
            }
        }
    }

    try {
        return selectDeviceByLoad(device_ids);
    } catch (const std::exception& e) {
        spdlog::warn("Schedule fallback to round robin: {}", e.what());
        return RoundRobin_Schedule(Ttype);
    }
}

struct Predict_data {
    double cpu_used;
    double xpu_used;
    double cpu_square;
    double xpu_square;
    double cpu_xpu;
    int platform;
    int tasktype;
};


// load model funtion
//void Docker_scheduler::loadModel(const std::string& model_path) {
//    if (!is_model_loaded) {
//        Ort::SessionOptions session_options;
//        onnx_session = new Ort::Session(env, model_path.c_str(), session_options);
//        is_model_loaded = true;
//        std::cout << "Model loaded successfully!" << std::endl;
//    } else {
//        std::cout << "Model already loaded, skipping reload." << std::endl;
//    }
//}
int Docker_scheduler::encodeTaskType(TaskType Ttype) {
    switch (Ttype) {
        case TaskType::Bert: return 0;
        case TaskType::MobileNet: return 1;
        case TaskType::ResNet50: return 2;
        case TaskType::YoloV5: return 3;
        case TaskType::deeplabv3: return 4;
        default: throw std::invalid_argument("Unknown TaskType");
    }
}

int Docker_scheduler::encodePlatform(DeviceType dtype) {
    switch (dtype) {
        case DeviceType::ATLAS_H: return 0;
        case DeviceType::ATLAS_L: return 1;
        case DeviceType::RK3588: return 2;
        default: throw std::invalid_argument("Unknown DeviceType");
    }
}
//Device Docker_scheduler::Model_predict(TaskType Ttype) {
//    // Initialize ONNX Runtime environment
//    std::string input_name = "double_input";
//    std::string output_name = "variable";
//    std::string model_path = "../../src/scheduler/predict.onnx";
//    // 在第一次调用推理时加载模型
//    if (!is_model_loaded) {
//        loadModel(model_path);  // 加载模型
//    }
//
//    // 使用 map 或 unordered_map，避免需要提前知道设备数量
//    std::unordered_map<DeviceID, Predict_data> predict_input;
//    // 为每个设备创建负载变量
//    for (const auto& [device_id, status] : device_status) {
//        predict_input[device_id].cpu_used=status.cpu_used;
//        predict_input[device_id].xpu_used=status.xpu_used;
//        //cout<<"device_id:"<<boost::uuids::to_string(device_id)<<" cpu_used:"<<status.cpu_used<<" xpu_used:"<<status.xpu_used<<endl;
//    }
//    for (const auto &[device_id, device] : device_static_info) {
//        predict_input[device_id].platform = encodePlatform(device.type);
//        predict_input[device_id].tasktype = encodeTaskType(Ttype);
//        //cout<<" platform:"<<device.type<<" tasktype:"<<Ttype<<endl;
//    }
//
//    // Prepare input data (match your model input format)
//    // 定义平台和对应的资源利用率（假设有3个平台）
//    std::vector<Predict_data> input_data;
//
//    for (const auto &[device_id, device] : device_static_info) {
//        double cpu=predict_input[device_id].cpu_used;
//        double npu=predict_input[device_id].xpu_used;
//        double cpu_square=cpu*cpu;
//        double npu_square=npu*npu;
//        double cpu_npu=cpu*npu;
//        input_data.push_back({predict_input[device_id].cpu_used,
//                              predict_input[device_id].xpu_used,
//                              cpu_square,
//                              npu_square,
//                              cpu_npu,
//                              predict_input[device_id].platform,
//                              predict_input[device_id].tasktype
//        });
//        //cout<<"input："<<predict_input[device_id].cpu_used<<" "<<predict_input[device_id].xpu_used<<" "<<predict_input[device_id].platform<<" "<<predict_input[device_id].tasktype<<endl;
//    }
//
//    // Create memory info object for input
//    Ort::MemoryInfo memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
//    std::vector<std::tuple<int, float, float>> platform_times;
//
//    // 对每个平台进行推理
//    for (size_t i = 0; i < input_data.size(); ++i) {
//        // 获取输入数据
//        const auto& platform_input = input_data[i];
//        std::vector<double> input_vector = {platform_input.cpu_used, platform_input.xpu_used,platform_input.cpu_square,platform_input.xpu_square,platform_input.cpu_xpu, static_cast<double>(platform_input.platform),static_cast<double>(platform_input.tasktype)};
//
//        // 创建输入张量
//        std::vector<int64_t> input_shape = {1, 7};  // 输入形状
//        Ort::Value input_tensor = Ort::Value::CreateTensor<double>(
//                memory_info, input_vector.data(), input_vector.size(), input_shape.data(), input_shape.size()
//        );
//
//        // 执行推理
//        std::vector<const char*> input_names = {input_name.c_str()};
//        std::vector<const char*> output_names = {output_name.c_str()};
//        std::vector<Ort::Value> output_tensors = onnx_session->Run(
//                Ort::RunOptions{nullptr}, input_names.data(), &input_tensor, 1, output_names.data(), 1
//        );
//
//        // 获取输出数据
//        const auto* output_data = output_tensors.front().GetTensorData<float>();
//
//        // 获取预测的端到端时间和执行时间
//        float ete_time = output_data[0];  // 预测的 ete_time
//        float exe_time = output_data[1];  // 预测的 exe_time
//        //std::cout << "predict all ete_time: " << ete_time << std::endl;
//        //std::cout << "predict al exec_time: " << exe_time << std::endl;
//        // 存储该平台的结果
//        platform_times.emplace_back(platform_input.platform, ete_time, exe_time);
//    }
//    // 选择ete_time最小的平台
//    auto min_ete_time_platform = std::min_element(platform_times.begin(), platform_times.end(),
//                                                  [](const std::tuple<int, float, float>& a, const std::tuple<int, float, float>& b) {
//                                                      return std::get<1>(a) < std::get<1>(b);  // 比较ete_time
//                                                  });
//
//    // 输出最优平台的预测结果
//    int devicetype=std::get<0>(*min_ete_time_platform);
//    //cout<<"devicetype:"<<devicetype<<endl;
//    std::cout << "predict ete_time: " << std::get<1>(*min_ete_time_platform) << std::endl;
//    //std::cout << "predict exec_time: " << std::get<2>(*min_ete_time_platform) << std::endl;
//    for (const auto &[device_id, device] : device_static_info) {
//        if (devicetype == encodePlatform(device.type) ) {
//            cout<<"DEVICE: ";
//            switch (device.type)
//            {
//                case RK3588:
//                std:cout<<"RK3588";
//                    break;
//                case ATLAS_L:
//                    cout<<"ATLAS_L";
//                    break;
//                case ATLAS_H:
//                    cout<<"ATLAS_H";
//                    break;
//                case ORIN:
//                    cout<<"ORIN";
//                    break;
//                default:
//                    cout<<"un_known?";
//                    break;
//            }
//            cout<<endl;
//            return device;
//        }
//    }
//    throw std::runtime_error("cannot decide a device");
//}

Device Docker_scheduler::Pic_Schedule(TaskType Ttype) {
    // Step 1. 快照一份设备ID列表
    std::vector<DeviceID> device_ids;
    {
        std::shared_lock<std::shared_mutex> lock(devs_mutex);
        device_ids.reserve(device_status.size());
        for (const auto& [device_id, _] : device_status) {
            device_ids.push_back(device_id);
        }
    } // 锁在这里释放，之后不再持有

    return selectDeviceByLoad(device_ids);
}


//轮询分配
Device Docker_scheduler::RoundRobin_Schedule(TaskType Ttype) {
    std::shared_lock<std::shared_mutex> lock(devs_mutex);
    auto start_time = std::chrono::high_resolution_clock::now();
    if (device_status.empty()) {
        throw std::runtime_error("No available devices for scheduling.");
    }

    //  获取设备 ID 列表
    std::vector<DeviceID> ids;
    if (Ttype != TaskType::Unknown) {
        for (const auto& [device_id, services] : device_active_services) {
            if (device_status.find(device_id) == device_status.end()) {
                continue;
            }
            if (std::find(services.begin(), services.end(), Ttype) != services.end()) {
                ids.push_back(device_id);
            }
        }
        if (ids.empty()) {
            auto it = tdMap.find(Ttype);
            if (it != tdMap.end()) {
                ids.reserve(it->second.size());
                for (const auto& [device_id, _] : it->second) {
                    if (device_status.find(device_id) != device_status.end()) {
                        ids.push_back(device_id);
                    }
                }
            }
        }
    }
    if (ids.empty()) {
        ids.reserve(device_status.size());
        for (const auto& [device_id, _] : device_status) {
            ids.push_back(device_id);
        }
    }

    //  取当前索引对应的设备
    DeviceID selected_id = ids[rr_index % ids.size()];

    //  更新索引，准备下次轮询
    rr_index = (rr_index + 1) % ids.size();

    spdlog::info("RoundRobin selected device: {}", device_static_info[selected_id].ip_address);
    // 记录结束时间
    auto end_time = std::chrono::high_resolution_clock::now();
    // 计算耗时（单位：毫秒）
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    spdlog::info("[RoundRobin] Execution time: {} ms", duration_ms);
    return device_static_info[selected_id];
}

bool Docker_scheduler::Disconnect_device(Device device) {
    // 从 device_status 中删除对应设备
    // 假设 device.global_id 是唯一标识
    try {
        bool removed = false;
        // 查找设备
        std::unique_lock<std::shared_mutex> lock(devs_mutex);
        auto it = device_status.find(device.global_id);
        if (it != device_status.end()) {
            device_status.erase(it);
            removed = true;
        } else {
            spdlog::warn("Device {} not found in device_status.", boost::uuids::to_string(device.global_id));
            return false;
        }
        lock.unlock();
        if (removed) {
            task_queue_manager_.RecoverTasks(device.global_id);
            spdlog::info("Device {} disconnected and removed.", boost::uuids::to_string(device.global_id));
            return true;
        }
    } catch (const std::exception &e) {
        spdlog::error("Disconnect_device exception: {}", e.what());
        return false;
    }
}
