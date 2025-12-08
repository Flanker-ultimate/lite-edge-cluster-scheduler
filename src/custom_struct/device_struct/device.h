//
// Created by lxsa1 on 22/10/2024.
//

#ifndef DEVICE_H
#define DEVICE_H

#include <atomic>
#include <iostream>
#include <string>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/uuid/string_generator.hpp>

#include "TimeCallback.h"
#include "nlohmann/json.hpp"
using json = nlohmann::json;
enum TaskType{
    YoloV5, MobileNet, Bert, ResNet50, deeplabv3, transcoding, decoding, encoding,Unknown
};
// 字符串到枚举的转换函数
TaskType StrToTaskType(const std::string& str);

//  NLOHMANN_JSON_SERIALIZE_ENUM generate to_json from_json method
NLOHMANN_JSON_SERIALIZE_ENUM(TaskType, {
    {TaskType::YoloV5, "YoloV5"},
    {TaskType::MobileNet, "MobileNet"},
    {TaskType::Bert, "Bert"},
    {TaskType::ResNet50, "ResNet50"},
    {TaskType::deeplabv3, "deeplabv3"},
    {TaskType::transcoding, "transcoding"},
    {TaskType::decoding, "decoding"},
    {TaskType::encoding, "encoding"},
    {TaskType::Unknown, "Unknown"}
})




enum DeviceType{
    RK3588, ATLAS_L, ATLAS_H, ORIN
};
NLOHMANN_JSON_SERIALIZE_ENUM(DeviceType,{
    {RK3588, "RK3588"},
    {ATLAS_L, "ATLAS_L"},
    {ATLAS_H, "ATLAS_H"},
    {ORIN, "ORIN"}
})

enum schduling_target{
    mini_latency, max_utilization, mini_power
};


typedef boost::uuids::uuid DeviceID;
struct Device{
    DeviceType type;
    DeviceID global_id;
    std::string ip_address;
    int agent_port;
    void show() {
        std::string ty;
        switch (type)
        {
            case RK3588:
                ty="RK3588";
            break;
            case ATLAS_L:
                ty="ATLAS_L";
            break;
            case ATLAS_H:
                ty="ATLAS_H";
            break;
            case ORIN:
                ty="ORIN";
            break;
            default:
                ty="un_known?";
            break;
        }
        printf(
                "Dev_id: %s\t"
                "dev type:%s\t"
                "ip:%s\t"
                "agent port:%d\n",
                boost::uuids::to_string(global_id).c_str(), ty.c_str(), ip_address.c_str(), agent_port
        );
    };
    void parseJson(const nlohmann::json& j) {
        try {
            j.at("type").get_to(type);
            boost::uuids::string_generator gen; // 创建字符串生成器
            std::string id_str = j.at("global_id").get<std::string>();
            global_id = gen(id_str); // 从字符串生成 UUID
            j.at("ip_address").get_to(ip_address);
            j.at("agent_port").get_to(agent_port);
        } catch (const nlohmann::json::exception& e) {
            std::cerr << "Error parsing JSON in Device::parseJson: " << e.what() << std::endl;
        }
    }

};

struct DeviceStatus{
    double mem_used;
    double cpu_used;
    double xpu_used;
    double net_latency;
    double net_bandwidth;
    double last_runtime;
    double disconnectTime;
    double reconnectTime;
    double timeWindow;
    void from_json(const json& j){
        j.at("mem").get_to(mem_used);
        j.at("cpu_used").get_to(cpu_used);
        j.at("xpu_used").get_to(xpu_used);
        j.at("net_latency").get_to(net_latency);
        j.at("net_bandwidth").get_to(net_bandwidth);
        //j.at("last_runtime").get_to(last_runtime);
        j.at("disconnectTime").get_to(disconnectTime);
        j.at("reconnectTime").get_to(reconnectTime);
        j.at("timeWindow").get_to(timeWindow);
    }
    void show(){
        printf(" mem_used:%f\tcpu_used:%f\txpu_used:%f\n",mem_used,cpu_used,xpu_used);
    }
    static DeviceStatus from_json_static(const json& j){
        DeviceStatus status;
        j.at("mem").get_to(status.mem_used);
        j.at("cpu_used").get_to(status.cpu_used);
        j.at("xpu_used").get_to(status.xpu_used);
        j.at("net_latency").get_to(status.net_latency);
        j.at("net_bandwidth").get_to(status.net_bandwidth);
        //j.at("last_runtime").get_to(status.last_runtime);
        j.at("disconnectTime").get_to(status.disconnectTime);
        j.at("reconnectTime").get_to(status.reconnectTime);
        j.at("timeWindow").get_to(status.timeWindow);
        return status;
    }
    json to_json(){
        json j;
        j["mem"]=this->mem_used;
        j["cpu_used"]=this->cpu_used;
        j["xpu_used"]=this->xpu_used;
        j["net_latency"]=this->net_latency;
        j["net_bandwidth"]=this->net_bandwidth;
        //j["last_runtime"]=this->last_runtime;
        j["disconnectTime"]=this->disconnectTime;
        j["reconnectTime"]=this->reconnectTime;
        j["timeWindow"]=this->timeWindow;
        return j;
    }
};

struct Task{
    int type;
    int global_id;

};

struct ImageInfo {
    // task images start params
    std::string container_name ;
    std::string image;
    std::vector<std::string> cmds;
    std::vector<std::string> args;
    bool host_config_privileged;
    std::vector<std::string> env;
    std::vector<std::string> host_config_binds;
    std::vector<std::string> devices;
    std::string host_ip;
    int host_port;
    int container_port;
    bool has_tty;
    std::string network_config;
    void parseJson(const json& j) {
        try {
            j.at("container_name").get_to(container_name);
            j.at("image").get_to(image);
            j.at("host_config_privileged").get_to(host_config_privileged);
            j.at("host_ip").get_to(host_ip);
            j.at("host_port").get_to(host_port);
            j.at("container_port").get_to(container_port);
            j.at("has_tty").get_to(has_tty);
            j.at("network_config").get_to(network_config);

            for (const auto& val : j.at("cmds")) { cmds.push_back(val.get<std::string>()); }
            for (const auto& val : j.at("args")) { args.push_back(val.get<std::string>()); }
            for (const auto& val : j.at("env")) { env.push_back(val.get<std::string>()); }
            for (const auto& val : j.at("host_config_binds")) { host_config_binds.push_back(val.get<std::string>()); }
            for (const auto& val : j.at("devices")) { devices.push_back(val.get<std::string>()); }
        } catch (const json::exception& e) {
            std::cerr << "Error parsing JSON in Image::parseJson: " << e.what() << std::endl;
        }
    }
};



struct TaskOverhead{
    // int id;
    // int device_id;
    double proc_time;
    double mem_usage;
    double cpu_usage;
    double xpu_usage;
};

struct TaskProfiling{
    int device;
    TaskOverhead overhead;
};

enum DevSrvInfoStatus {
    NoExist,
    Creating, // vector<SrvInfo> srv_infos has only one Creating Srv instance
    Running,
    Deleting
};

struct SrvInfo {
    std::string container_id;
    std::string ip;
    int port; // host_port

};


//all serves of  a kind of task on a device
struct DevSrvInfos {
    DevSrvInfoStatus dev_srv_info_status; // (task,device)->info
    TimerCallback timer_callback;
    std::vector<SrvInfo> srv_infos; // the port of every service
    DevSrvInfos() : dev_srv_info_status(DevSrvInfoStatus::NoExist) {}
    DevSrvInfos(const DevSrvInfos &other)
        : dev_srv_info_status(other.dev_srv_info_status),
          srv_infos(other.srv_infos) {
    }
};


// TODO this func optomize as config_file
std::string GetDockerVersion(const Device& dev);

#endif // DEVICE_H
