#include"scheduler.h"
#include<iostream>
#include<fstream>
#include<thread>
#include<chrono>
#include <DockerClient.h>
#include <random>
std::map<TaskType, std::map<DeviceType, StaticInfoItem> > Docker_scheduler::static_info; // static task info

std::shared_mutex Docker_scheduler::devs_mutex; //
std::map<DeviceID, Device> Docker_scheduler::device_static_info; // static device info

std::map<DeviceID, DeviceStatus> Docker_scheduler::device_status; // dynamic device info

// std::shared_mutex Docker_scheduler::td_map_mutex_; // Thread-safe mutex for TDMap
std::map<TaskType, std::map<DeviceID, DevSrvInfos> > Docker_scheduler::tdMap;
z3::context Docker_scheduler::c;
std::mutex Docker_scheduler::z3_mutex;
// onnx
//Ort::Env Docker_scheduler::env(ORT_LOGGING_LEVEL_WARNING, "OnnxModel");
//Ort::Session* Docker_scheduler::onnx_session = nullptr;
bool Docker_scheduler::is_model_loaded = false;
size_t Docker_scheduler::rr_index = 0;
double get_double_value(const expr &e) {
    if (e.is_numeral()) {
        if (e.is_int()) {
            return e.get_numeral_int();
        } else {
            int num = e.numerator().get_numeral_int();
            int denom = e.denominator().get_numeral_int();
            return static_cast<double>(num) / denom;
        }
    }
    return 0.0;
}

std::string double_to_string(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(6) << value;
    return oss.str();
}

Docker_scheduler::Docker_scheduler() {
}

Docker_scheduler::Docker_scheduler(string knowledge_file) {

    ifstream infile;
    infile.open(knowledge_file, ios::in);
    //reading profiling result from infile, write them into tasks_info
    if (!infile.is_open()) {
        cerr << "Failed to open file: " << knowledge_file << endl;
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
        cout << "task load:" << task_str << endl;
        // Loop through the devices for each task
        for (const auto &device_entry: task_entry.value().items()) {
            string device_str = device_entry.key();
            DeviceType device_type;
            if (device_str == "RK3588") device_type = RK3588;
            else if (device_str == "ATLAS_L") device_type = ATLAS_L;
            else if (device_str == "ATLAS_H") device_type = ATLAS_H;
            else if (device_str == "ORIN") device_type = ORIN;
            else continue;
            cout << "device load:" << device_str << endl;
            // Check if the required fields are present in the JSON before creating StaticInfoItem
            const auto &device = device_entry.value();
            if (device.contains("imageInfo") && device.contains("taskOverhead")) {
                // Create StaticInfoItem for the device only if the required fields exist
                StaticInfoItem static_info_item(device);

                // Add the StaticInfoItem to the static_info map
                static_info[task_type][device_type] = static_info_item;
            } else {
                cout << "Missing necessary fields for device: " << device_str << endl;
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
            cout << "Device Type: " << device_static_info[id].type << endl;
            cout << "Device Status: " << endl;
            status.show();
        }
    }
}


void Docker_scheduler::init(string filepath) {
    loadStaticInfo(filepath);
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
                                spdlog::error(
                                        "Failed to get device info, agent return filed,dev.ip_address:{}, dev.agent_port:{}",
                                        dev.ip_address, dev.agent_port);
                                continue;
                            }
                            DeviceStatus status;
                            status.from_json(j["result"]);

                            auto it = device_status.find(k);
                            if (it != device_status.end()) {
                                it->second = status;  // 更新已有设备的状态
                            }
                        } else {
                            spdlog::error("Failed to get device info, dev.ip_address:{}, dev.agent_port:{}",
                                          dev.ip_address, dev.agent_port);
                            continue;
                        }
                    } catch (const std::exception &e) {
                        std::cerr << "collect info error: " << e.what() << std::endl;
                        continue;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }).detach();
}




// CallBack Function to delete inactive contianer
void Docker_scheduler::inactiveTimeCallback(TaskType ttype, Device dev, string container_id) {
    sleep(2); // sleep to wait questing toi finish to avoid the current quest failed
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

    std::cout << "inactiveTimeCallback triggered the callback!" << std::endl;
}

std::optional<SrvInfo> Docker_scheduler::getOrCrtSrvByTType(TaskType ttype) {
    if (tdMap.find(ttype) == tdMap.end()) {
        spdlog::error("No available service nodes to support this task type:{}", to_string(nlohmann::json(ttype)));
        return std::nullopt;
    }
    // step 1: use z3s get deviceID
    TimeRecord<chrono::milliseconds> z3("z3");
    z3.startRecord();
    Device tgt_dev = getTgtDevByTtypeAndDevIds(ttype);
    z3.endRecord();
    spdlog::info("z3 cost_time:{}", z3.getDuration());
    z3.clearRecord();

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
                spdlog::error(
                    "for Tasktype:{},devIp:{}, the dev is creating contianer but over 10 times try or creating failed",
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
    static int callCount = 0;
    static Device tgt_dev;
    //return Z3_simulate_schedule(ttype, 0, 1, 0); //ATLAS-H ATLAS-L RK3588
    //tgt_dev = Z3_schedule_v2(ttype);
    //tgt_dev = Model_predict(ttype);
    tgt_dev = Z3_schedule_v2(ttype);
    return tgt_dev;
}


Device Docker_scheduler::getTgtDevByTtypeAndDevIds(TaskType ttype, vector<DeviceID> devIds) {
    DeviceID tgt_dev_id = devIds[0];
    return Z3_schedule_v2(ttype);
}


std::map<TaskType, std::map<DeviceType, StaticInfoItem> > Docker_scheduler::getStaticInfo() {
    return static_info;
}

//Device Docker_scheduler::Z3_schedule(TaskType Ttype) {
//    std::cout << "Executing Z3_schedule..." << std::endl;
//    TimeRecord<chrono::milliseconds> z3("z3");
//    z3.startRecord();
//    // 创建 Z3 上下文与优化器
//    context c;
//    optimize opt(c);
//
//    // 保存设备的 Z3 变量和其对应的负载表达式
//    std::map<DeviceID, std::optional<expr>> device_cpu_load_vars;
//    std::map<DeviceID, std::optional<expr>> device_mem_load_vars;
//    std::map<DeviceID, std::optional<expr>> device_xpu_load_vars;
//
//    std::vector<expr> load_differences;
//
//    // 为每个设备创建负载变量
//    for (const auto& [device_id, status] : device_status) {
//        device_cpu_load_vars[device_id] = c.real_val(std::to_string(status.cpu_used).c_str());
//        device_mem_load_vars[device_id] = c.real_val(std::to_string(status.mem_used).c_str());
//        device_xpu_load_vars[device_id] = c.real_val(std::to_string(status.xpu_used).c_str());
//    }
//
//    // 添加任务的 Profiling 数据为约束
//    expr maxV = c.real_val(double_to_string(1.0).c_str());
//    const auto& task_profiling = static_info[Ttype];
//    for (const auto& [device_type, profiling_info] : task_profiling) {
//        for (const auto& [device_id, status] : device_status) {
//            if (device_static_info[device_id].type == device_type) {
//                // 约束：任务的资源需求不能超过设备剩余容量
//                expr prof_cpu = c.real_val(double_to_string(profiling_info.taskOverhead.cpu_usage).c_str());
//                expr prof_mem = c.real_val(double_to_string(profiling_info.taskOverhead.mem_usage).c_str());
//                expr prof_xpu = c.real_val(double_to_string(profiling_info.taskOverhead.xpu_usage).c_str());
//
//                expr cpu = *device_cpu_load_vars[device_id] + prof_cpu;
//                expr mem = *device_mem_load_vars[device_id] + prof_mem;
//                expr xpu = *device_xpu_load_vars[device_id] + prof_xpu;
//                if(device_type==DeviceType::ATLAS_L){//针对低算力板子做约束
//                    z3::expr factor = c.real_val("1.5");
//                    cpu = cpu * factor;
//                    mem = mem * factor;
//                    xpu = xpu * factor;
//                }
//
//                // std::cout << "Initial load for Device " << boost::uuids::to_string(device_id)
//                //         << " CPU: " << get_double_value(*device_cpu_load_vars[device_id])
//                //         << " MEM: " << get_double_value(*device_mem_load_vars[device_id])
//                //         << " XPU: " << get_double_value(*device_xpu_load_vars[device_id])
//                //         << std::endl;
//                // std::cout << "Task Profiling Data for Device " << boost::uuids::to_string(device_id)
//                //         << " CPU: " << get_double_value(prof_cpu)
//                //         << " MEM: " << get_double_value(prof_mem)
//                //         << " XPU: " << get_double_value(prof_xpu)
//                //         << std::endl;
//                // std::cout << "Estimated load exprs for Device " << boost::uuids::to_string(device_id)
//                //         << " Estimated CPU: " << get_double_value(cpu)
//                //         << " Estimated MEM: " << get_double_value(mem)
//                //         << " Estimated XPU: " << get_double_value(xpu)
//                //         << std::endl;
//                //getchar();
//                opt.add(cpu <= maxV);
//                opt.add(mem <= maxV);
//                opt.add(xpu <= maxV);
//            }
//        }
//    }
//
//
//    expr weighted_load = c.real_val(0);
//    for (const auto& [device_id, load_var] : device_cpu_load_vars) {
//        weighted_load = weighted_load +
//                        (0.33 * *device_cpu_load_vars[device_id] +
//                        0.33 * *device_mem_load_vars[device_id] +
//                        0.33 * *device_xpu_load_vars[device_id]);
//    }
//    opt.minimize(weighted_load);
//
//    // 求解
//    if (opt.check() == sat) {
//        model m = opt.get_model();
//        DeviceID selected_device_id;
//        double min_load = std::numeric_limits<double>::max();
//
//        // 查找负载最小的设备
//        for (const auto& [device_id, load_var] : device_cpu_load_vars) {
//            double cpu_load = get_double_value(m.eval(*load_var));
//            double mem_load = get_double_value(m.eval(*device_mem_load_vars[device_id]));
//            double xpu_load = get_double_value(m.eval(*device_xpu_load_vars[device_id]));
//            double total_load = cpu_load + mem_load + xpu_load;
//        //     std::cout << "Device status after schedule" << boost::uuids::to_string(device_id)
//        //   << " CPU: " << get_double_value(m.eval(*load_var))
//        //   << " MEM: " << get_double_value(m.eval(*device_mem_load_vars[device_id]))
//        //   << " XPU: " << get_double_value(m.eval(*device_xpu_load_vars[device_id]))
//        //   << std::endl;
//        //     getchar();
//            if (cpu_load <= 1.0 && mem_load <= 1.0 && xpu_load <= 1.0) { // 确保约束生效
//                double total_load = cpu_load + mem_load + xpu_load;
//                if (total_load < min_load) {
//                    min_load = total_load;
//                    selected_device_id = device_id;
//                }
//            }
//        }
//
//        std::cout << "Selected device with load: " << min_load << std::endl;
//        z3.endRecord();
//        z3.print();
//        z3.clearRecord();
//        return device_static_info[selected_device_id];
//    } else {
//        z3.endRecord();
//        z3.print();
//        z3.clearRecord();
//        DeviceID selected_device_id = device_static_info.begin()->first;
//        cout<<"Z3 could not find a suitable device, select the first device by default."<<endl;
//        return device_static_info[selected_device_id]; // 默认返回第一个设备
//        //throw std::runtime_error("Z3 could not find a suitable device.");
//    }
//}

//尝试修改调度单位为单个任务
Device Docker_scheduler::Z3_schedule_v2(TaskType Ttype){
    std::lock_guard<std::mutex> lock(z3_mutex);
    //std::cout << "Executing Z3_schedule..." << std::endl;

    optimize opt(c);

    // 保存设备的 Z3 变量和其对应的负载表达式
    std::map<DeviceID, std::optional<expr>> device_cpu_load_vars;
    std::map<DeviceID, std::optional<expr>> device_mem_load_vars;
    std::map<DeviceID, std::optional<expr>> device_xpu_load_vars;

    std::vector<expr> load_differences;

    // 为每个设备创建负载变量
    for (const auto& [device_id, status] : device_status) {
        device_cpu_load_vars[device_id] = c.real_val(std::to_string(status.cpu_used).c_str());
        device_mem_load_vars[device_id] = c.real_val(std::to_string(status.mem_used).c_str());
        device_xpu_load_vars[device_id] = c.real_val(std::to_string(status.xpu_used).c_str());
    }

    // 添加任务的 Profiling 数据为约束
    expr maxV = c.real_val(double_to_string(1.0).c_str());
    const auto& task_profiling = static_info[Ttype];
    
    for (const auto& [device_type, profiling_info] : task_profiling) {
        for (const auto& [device_id, status] : device_status) {
            if (device_static_info[device_id].type == device_type) {
                // 约束：任务的资源需求不能超过设备剩余容量
                expr prof_cpu = c.real_val(double_to_string(profiling_info.taskOverhead.cpu_usage).c_str());
                expr prof_mem = c.real_val(double_to_string(profiling_info.taskOverhead.mem_usage).c_str());
                expr prof_xpu = c.real_val(double_to_string(profiling_info.taskOverhead.xpu_usage).c_str());
                
                //首先判断是否有任务在运行，如果没有意味着需要冷启动，则应该将profiling数据加入约束
                if(tdMap[Ttype][device_id].dev_srv_info_status == NoExist){
                    expr cpu = *device_cpu_load_vars[device_id] + prof_cpu;
                    expr mem = *device_mem_load_vars[device_id] + prof_mem;
                    expr xpu = *device_xpu_load_vars[device_id] + prof_xpu;
                    device_cpu_load_vars[device_id] = cpu;
                    device_mem_load_vars[device_id] = mem;
                    device_xpu_load_vars[device_id] = xpu;
                    opt.add(cpu <= maxV);
                    opt.add(mem <= maxV);
                    opt.add(xpu <= maxV);
                }else{
                    //如果有任务在运行，就不需要加入profiling数据
                    expr cpu = *device_cpu_load_vars[device_id];
                    expr mem = *device_mem_load_vars[device_id];
                    expr xpu = *device_xpu_load_vars[device_id];
//                    if(device_type==DeviceType::ATLAS_L){//针对低算力板子做约束
//                        z3::expr factor = c.real_val("1.5");
//                        cpu = cpu * factor;
//                        mem = mem * factor;
//                        xpu = xpu * factor;
//                        cout<<"choose atlas_l"<<endl;
//                    }
                    opt.add(cpu <= maxV);
                    opt.add(mem <= maxV);
                    opt.add(xpu <= maxV);

                    std::cout << "Estimated load exprs for Device " << boost::uuids::to_string(device_id)
                            << " Estimated CPU: " << get_double_value(cpu)
                            << " Estimated MEM: " << get_double_value(mem)
                            << " Estimated XPU: " << get_double_value(xpu)
                            << std::endl;
                }


                // std::cout << "Initial load for Device " << boost::uuids::to_string(device_id)
                //         << " CPU: " << get_double_value(*device_cpu_load_vars[device_id])
                //         << " MEM: " << get_double_value(*device_mem_load_vars[device_id])
                //         << " XPU: " << get_double_value(*device_xpu_load_vars[device_id])
                //         << std::endl;
                // std::cout << "Task Profiling Data for Device " << boost::uuids::to_string(device_id)
                //         << " CPU: " << get_double_value(prof_cpu)
                //         << " MEM: " << get_double_value(prof_mem)
                //         << " XPU: " << get_double_value(prof_xpu)
                //         << std::endl;
                // std::cout << "Estimated load exprs for Device " << boost::uuids::to_string(device_id)
                //         << " Estimated CPU: " << get_double_value(cpu)
                //         << " Estimated MEM: " << get_double_value(mem)
                //         << " Estimated XPU: " << get_double_value(xpu)
                //         << std::endl;
                //getchar();
            }
        }
    }
    

    expr weighted_load = c.real_val(0);
    for (const auto& [device_id, load_var] : device_cpu_load_vars) {
        weighted_load = weighted_load +
                        (0.5 * *device_cpu_load_vars[device_id] +
                        0.0 * *device_mem_load_vars[device_id] +
                        0.5 * *device_xpu_load_vars[device_id]);
    }
    opt.minimize(weighted_load);

    // 求解
    if (opt.check() == sat) {
        model m = opt.get_model();
        DeviceID selected_device_id;
        double min_load = std::numeric_limits<double>::max();

        // 查找负载最小的设备
        for (const auto& [device_id, load_var] : device_cpu_load_vars) {
            double cpu_load = get_double_value(m.eval(*load_var));
            double mem_load = get_double_value(m.eval(*device_mem_load_vars[device_id]));
            double xpu_load = get_double_value(m.eval(*device_xpu_load_vars[device_id]));
            //double total_load = cpu_load + xpu_load;
//            std::cout << "Device status after schedule" << boost::uuids::to_string(device_id)
//          << " CPU: " << get_double_value(m.eval(*load_var))
//          << " MEM: " << get_double_value(m.eval(*device_mem_load_vars[device_id]))
//          << " XPU: " << get_double_value(m.eval(*device_xpu_load_vars[device_id]))
//          << std::endl;
            //getchar();
            if (cpu_load <= 1.0 && mem_load <= 1.0 && xpu_load <= 1.0) { // 确保约束生效
                double total_load = cpu_load+ xpu_load;
                if (total_load < min_load) {
                    min_load = total_load;
                    selected_device_id = device_id;
                }
            }else{
                throw std::runtime_error("Z3 could not find a suitable device.");
            }

        }

        std::cout << "Selected device: " << boost::uuids::to_string(selected_device_id) << " with load: " << min_load << std::endl;

        return device_static_info[selected_device_id];
    } else {
        DeviceID selected_device_id = device_static_info.begin()->first;
        cout<<"Z3 could not find a suitable device, select the first device by default."<<endl;
        return device_static_info[selected_device_id]; // 默认返回第一个设备
    }
}
    

Device Docker_scheduler::Z3_simulate_schedule(TaskType Ttype,float prob1, float prob2, float prob3) {
     //find device_static_info and print dev informations
    std::vector<DeviceID> device_ids;
    for (const auto &[device_id, device]: device_static_info) {
        // print dev information
        //std::cout << "Device ID: " << device_id << ", Type: " << device.type << std::endl;
        device_ids.push_back(device_id);
    }
    // check the number of devices
    // if (device_ids.size() < 3) {
    //     throw std::runtime_error("设备数量不足3个，无法根据概率选择设备");
    // }

    // check the sum of probabilities
    if (prob1 + prob2 + prob3 != 1.0f) {
        throw std::invalid_argument("输入的概率参数不合法：prob1 + prob2 + prob3 必须等于 1");
    }

    // produce a random number
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(0.0, 1.0);

    float random_value = dis(gen);

// choose the device based on the random value
    for (const auto &[device_id, device] : device_static_info) {
        if (device.type == DeviceType::ATLAS_H && random_value >= 0.0f && random_value < prob1) {
            cout << "DEVICE ATLAS-H" << endl;
            return device;
        } else if (device.type == DeviceType::ATLAS_L && random_value >= prob1 && random_value < prob1 + prob2) {
            cout << "DEVICE ATLAS-L" << endl;
            return device;
        } else if (device.type == DeviceType::RK3588 && random_value >= prob1 + prob2 && random_value < 1.0f) {
            cout << "DEVICE RK3588" << endl;
            return device;
        }
    }
    throw std::runtime_error("can not decide a device");

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

    // 假设权重
    const double w_cpu = 0.3;
    const double w_mem = 0.1;
    const double w_xpu = 0.4;
    const double w_bandwidth = 1;
    const double w_net_latency = 1;
    const double w_runtime = 0.2;

    //选出负载最低的设备
    DeviceID best_device;
    double min_load = std::numeric_limits<double>::max();
    // 遍历所有设备
    for (const auto& device_id : device_ids) {
        std::shared_lock<std::shared_mutex> lock(devs_mutex);
        auto it = device_status.find(device_id);
        if (it == device_status.end()) {
            // 在这期间设备可能已被删除，跳过
            continue;
        }
        const auto& status = it->second;
        double cpu_val = status.cpu_used;
        double mem_val = status.mem_used;
        double xpu_val = status.xpu_used;
        double bandwidth_val = status.net_bandwidth;
        double net_latency_val=status.net_latency;
        //double last_runtime_val=status.last_runtime;
        // 计算加权负载
        double load = w_cpu * cpu_val +
                      w_mem * mem_val +
                      w_xpu * xpu_val +
                      w_bandwidth * bandwidth_val +
                      w_net_latency * net_latency_val;

        if (load < min_load) {
            min_load = load;
            best_device = device_id;
        }
        std::cout<<"caculate device" << device_static_info[device_id].ip_address << std::endl;
    }

    std::cout << "Selected device: " << device_static_info[best_device].ip_address << std::endl;
    return device_static_info[best_device];
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
    ids.reserve(device_status.size());
    for (const auto& [device_id, _] : device_status) {
        ids.push_back(device_id);
    }

    //  取当前索引对应的设备
    DeviceID selected_id = ids[rr_index % ids.size()];

    //  更新索引，准备下次轮询
    rr_index = (rr_index + 1) % ids.size();

    std::cout << "[INFO] RoundRobin selected device: "
              << device_static_info[selected_id].ip_address << std::endl;
    // 记录结束时间
    auto end_time = std::chrono::high_resolution_clock::now();
    // 计算耗时（单位：毫秒）
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

    std::cout << "[RoundRobin] Execution time: " << duration_ms << " ms" << std::endl;
    return device_static_info[selected_id];
}

bool Docker_scheduler::Disconnect_device(Device device) {
    // 从 device_status 中删除对应设备
    // 假设 device.global_id 是唯一标识
    try {
        // 查找设备
        std::unique_lock<std::shared_mutex> lock(devs_mutex);
        auto it = device_status.find(device.global_id);
        if (it != device_status.end()) {
            device_status.erase(it);
            std::cout << "[INFO] Device " << device.global_id
                      << " disconnected and removed." << std::endl;
            return true;
        } else {
            std::cerr << "[WARN] Device " << device.global_id
                      << " not found in device_status." << std::endl;
            return false;
        }
    } catch (const std::exception &e) {
        std::cerr << "[ERROR] Disconnect_device exception: " << e.what() << std::endl;
        return false;
    }
}