#include "HttpServer.h"
#include "httplib.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <cstring>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <utility>
#include "DockerClient.h"
#include "TimeRecorder.h"
#include <filesystem>
using json = nlohmann::json;

HttpServer::HttpServer(std::string ip, const int port, string absoulte_config_path) : ip(std::move(ip)), port(port) {
}

bool HttpServer::Start() {
    httplib::Server svr;
    // 启用多个工作线程
//    svr.new_task_queue = [] {
//        return new httplib::ThreadPool(64);
//    };
    // 注册路由
    svr.Post(QUSET_ROUTE, this->HandleQuest);
    svr.Post(REGISTER_NODE_ROUTE, this->HandleRegisterNode);
    svr.Post("/hot_start", this->HandleHotStart);
    svr.Post(SCHEDULE_ROUTE, this->HandleSchedule);
    svr.Post(DISCONNECT_NODE_ROUTE, this->HandleDisconnect);
    svr.Post(SCHEDULE_ROUND_ROUTE, this->HandleScheduleRound);

    spdlog::info("HttpServer started success，ip:{} port:{}",this->ip, this->port);
    auto result = svr.listen(this->ip, this->port);
    if (!result) {
        spdlog::error("HttpServer start failed！");
        return false;
    }
    return true;
}

// Define a function to modify or insert a key-value pair
void modifyOrInsert(httplib::Headers &headers,
                    const std::string &key, const std::string &newValue) {
    // Check if the key exists
    auto range = headers.equal_range(key);
    if (range.first != range.second) {
        // If found, modify the value for all matching keys
        for (auto it = range.first; it != range.second; ++it) {
            it->second = newValue;
        }
    } else {
        // If not found, insert a new key-value pair
        headers.emplace(key, newValue);
    }
}

//
// transfer request1
// example url: requet?taskid=0&real_url=hello/lxs
void HttpServer::HandleQuest(const httplib::Request &req, httplib::Response &res) {
    TimeRecord<std::chrono::milliseconds> time_record("HandleQuest");
    time_record.startRecord();

    TimeRecord<std::chrono::milliseconds> time_record_schedule("schedule");
    time_record_schedule.startRecord();

    // parse params
    auto task_type_str = req.get_param_value("taskid");
    auto real_url_param = req.get_param_value("real_url");
    TaskType task_type;
    // validate params
    try {
        task_type = StrToTaskType(task_type_str);
    } catch (const std::invalid_argument &e) {
        res.status = 400; // Bad Request
        res.set_content("invalid format taskid", "text/plain");
        return;
    }
    if (task_type == TaskType::Unknown) {
        res.status = 400; // Bad Request
        res.set_content("Missing taskid or real_url parameter", "text/plain");
        return;
    }
    if (real_url_param.empty()) {
        res.status = 400; // Bad Request
        res.set_content("Missing taskid or real_url parameter", "text/plain");
        return;
    }
    // get target_device_id
    optional<SrvInfo> srv_info_opt = Docker_scheduler::getOrCrtSrvByTType(task_type);
    if (srv_info_opt == nullopt) {
        res.status = 400; // Bad Request
        res.set_content("we can't get a useful srv", "text/plain");
        return;
    }
    // print in the end of quest
    time_record_schedule.endRecord();

    SrvInfo srv_info = srv_info_opt.value();
    string origin_host_ip = req.remote_addr;
    int origin_host_port = req.remote_port;
    string transfer_host_ip = req.local_addr;
    int transfer_host_port = req.local_port;
    string tgt_host_ip = srv_info.ip;
    int tgt_host_port = srv_info.port;

    httplib::Client cli(tgt_host_ip, tgt_host_port);
    // Forward the request to the target host.
    httplib::MultipartFormDataItems items;
    for (auto file: req.files) {
        items.push_back(file.second);
    }

    httplib::Result response;
    try {
        response = cli.Post("/" + real_url_param, items);
    } catch (const std::exception &e) {
        spdlog::error("{} Error sending request: {}", req.path, e.what());
        res.status = 500;
        res.set_content("Internal Server Error", "text/plain");
        return;
    }

    if (response != nullptr && response->status != -1) {
        // return to client
        res.status = response->status;
        res.set_header("Content-Type", response->get_header_value("Content-Type"));

        time_record.endRecord();
        // append gateway_time to response

        spdlog::info(
            "URL: {},task_type_str:{}, real_url_param:{} origin_host_ip: {}:{}, transfer_host_ip: {}:{}, tgt_host_ip: {}:{}, duration_time:{}, time_record_schedule:{}",
            req.path,
            task_type_str,
            real_url_param,
            origin_host_ip,
            origin_host_port,
            transfer_host_ip,
            transfer_host_port,
            tgt_host_ip,
            tgt_host_port,
            time_record.getDuration(),
            time_record_schedule.getDuration()
        );
        nlohmann::json jsonData = nlohmann::json::parse(response->body);
        jsonData["gateway_time"] = (double) (time_record.getDuration());
        res.body = jsonData.dump();
    } else {
        res.status = 502;
        time_record.endRecord();
        res.set_content("Bad Gateway", "text/plain");
        spdlog::error("URL: {},task_type_str:{}, real_url_param:{} origin_host_ip: {}:{}, transfer_host_ip: {}:{}, tgt_host_ip: {}:{}, duration_time:{}, Error:{}",
            req.path,
            task_type_str,
            real_url_param,
            origin_host_ip,
            origin_host_port,
            transfer_host_ip,
            transfer_host_port,
            tgt_host_ip,
            tgt_host_port,
            time_record.getDuration(),
            "Connection failed or response = nullptr"
        );
    }
}

void HttpServer::HandleRegisterNode(const httplib::Request &req, httplib::Response &res) {
    nlohmann::json jsonData = nlohmann::json::parse(req.body);
    Device device;
    device.parseJson(jsonData);
    Docker_scheduler::RegisNode(device);
    res.status = 200;
    res.set_content("Node registered successfully", "text/plain");
    spdlog::info("Node registered successfully, param:{}", req.body);
}

void HttpServer::HandleHotStart(const httplib::Request &req, httplib::Response &res) {
    auto task_type_str = req.get_param_value("taskid");
    TaskType task_type = StrToTaskType(task_type_str);
    bool ret = Docker_scheduler::HotStartAllNodeByTType(task_type);
    if (ret) {
        res.set_content("HotStart successfully", "text/plain");
        spdlog::info("HotStart successfully, task_type:{}", to_string(nlohmann::json(task_type)));
        res.status = 200;
    }else {
        res.set_content("HotStart failed", "text/plain");
        spdlog::info("HotStart failed, task_type:{}", to_string(nlohmann::json(task_type)));
        res.status = 400;
    }
}


//新的调度接口
void HttpServer::HandleSchedule(const httplib::Request &req, httplib::Response &res) {
    namespace fs = std::filesystem;
    fs::path currentPath = fs::current_path();
    spdlog::info("Current path: {}", currentPath.string());
    try {
        TimeRecord<std::chrono::milliseconds> time_record_schedule("schedule");
        time_record_schedule.startRecord();

        spdlog::info("请求接收");
        auto body_json = nlohmann::json::parse(req.body);

        // 检查必要字段
        if (!body_json.contains("ip") ||
            !body_json.contains("filepath")
            ){
            res.status = 400;
            res.set_content(R"({"status":"error","msg":"missing required fields"})", "application/json");
            return;
        }
        // 解析字段
        std::string ip        = body_json["ip"].get<std::string>();
        TaskType tasktype  = StrToTaskType(body_json["tasktype"].get<std::string>());
        std::string filepath  = body_json["filepath"].get<std::string>();
        std::string filename  = body_json["filename"].get<std::string>();

        // 拼接完整路径，例如 "192.168.43.219/" + "192.168.43.219_100.png"
        if (!filepath.empty() && filepath.back() != '/') {
            filepath += '/';
        }
        std::string _fullpath = filepath + filename;
        // 直接使用相对路径（忽略原有的filepath字段）
        std::string fullpath = "../../../src/modules/master/files/pic/" + _fullpath;
        //std::cout << "当前路径: " << std::filesystem::current_path() << std::endl;


        // 读取图片
        std::ifstream ifs(fullpath, std::ios::binary);
        if (!ifs) {
            spdlog::error("无法读取图片文件: {}", fullpath);
            res.status = 500;
            res.set_content(R"({"status":"error","msg":"failed to open image file"})", "application/json");
            return;
        }
        std::ostringstream buffer;
        buffer << ifs.rdbuf();
        std::string image_data = buffer.str();

        //调度程序选择最佳设备
        Device device = Docker_scheduler::Pic_Schedule(tasktype);
        std::string target_ip = device.ip_address;
        int target_port = 20810;
        //int target_port = device.agent_port;
        //std::string target_ip="172.28.16.1";



        // 准备 JSON 元信息
        nlohmann::json meta_json;
        meta_json["ip"] = ip;
        meta_json["file_name"] = filename;
        meta_json["tasktype"] = body_json["tasktype"].get<std::string>();
        std::string meta_str = meta_json.dump();

        //发送请求
        httplib::Client cli(target_ip, target_port);
        // multipart 表单
        httplib::MultipartFormDataItems form_items = {
                {"pic_file", image_data, filename, "image/png"},
                {"pic_info", meta_str, "", "application/json"}
        };
        auto res_send = cli.Post("/recv_task", form_items);
        spdlog::info("请求发送");

        spdlog::info("Gateway time:{}",time_record_schedule.getDuration());
        if (res_send && res_send->status == 200) {
            spdlog::info("已成功将图片发送到 {}", target_ip);
            res.status = 200;
            res.set_content(R"({"status":"send success"})", "application/json");
        } else {
            spdlog::error("发送图片失败");
            res.status = 500;
            res.set_content(R"({"status":"error","msg":"send to device failed"})", "application/json");
        }


    } catch (const std::exception &e) {
        spdlog::error("解析失败: {}", e.what());
        res.status = 400;
        res.set_content(R"({"status":"error","msg":"invalid json"})", "application/json");
    }
}


//新的调度接口
void HttpServer::HandleScheduleRound(const httplib::Request &req, httplib::Response &res) {
    namespace fs = std::filesystem;
    fs::path currentPath = fs::current_path();
    //std::cout << "Current path: " << currentPath << std::endl;
    try {
        auto body_json = nlohmann::json::parse(req.body);

        // 检查必要字段
        if (!body_json.contains("ip") ||
            !body_json.contains("filepath")
                ){
            res.status = 400;
            res.set_content(R"({"status":"error","msg":"missing required fields"})", "application/json");
            return;
        }
        // 解析字段
        std::string ip        = body_json["ip"].get<std::string>();
        TaskType tasktype  = StrToTaskType(body_json["tasktype"].get<std::string>());
        std::string filepath  = body_json["filepath"].get<std::string>();
        std::string filename  = body_json["filename"].get<std::string>();

        // 拼接完整路径，例如 "192.168.43.219/" + "192.168.43.219_100.png"
        if (!filepath.empty() && filepath.back() != '/') {
            filepath += '/';
        }
        std::string fullpath = filepath + filename;

        //调度程序选择最佳设备
        Device device = Docker_scheduler::RoundRobin_Schedule(tasktype);
        std::string target_ip = device.ip_address;
//        int target_port = device.agent_port;
//        std::string target_ip="172.28.16.1";
        int target_port = 20810;
        // 读取图片
        std::ifstream ifs(fullpath, std::ios::binary);
        if (!ifs) {
            spdlog::error("无法读取图片文件: {}", fullpath);
            res.status = 500;
            res.set_content(R"({"status":"error","msg":"failed to open image file"})", "application/json");
            return;
        }
        std::ostringstream buffer;
        buffer << ifs.rdbuf();
        std::string image_data = buffer.str();


        // 准备 JSON 元信息
        nlohmann::json meta_json;
        meta_json["ip"] = ip;
        meta_json["file_name"] = filename;
        meta_json["tasktype"] = body_json["tasktype"].get<std::string>();
        std::string meta_str = meta_json.dump();

        //发送请求
        httplib::Client cli(target_ip, target_port);
        // multipart 表单
        httplib::MultipartFormDataItems form_items = {
                {"pic_file", image_data, filename, "image/png"},
                {"pic_info", meta_str, "", "application/json"}
        };
        auto res_send = cli.Post("/recv_task", form_items);

        if (res_send && res_send->status == 200) {
            spdlog::info("已成功将图片发送到 {}", target_ip);
            res.status = 200;
            res.set_content(R"({"status":"send success"})", "application/json");
        } else {
            spdlog::error("发送图片失败");
            res.status = 500;
            res.set_content(R"({"status":"error","msg":"send to device failed"})", "application/json");
        }


    } catch (const std::exception &e) {
        spdlog::error("解析失败: {}", e.what());
        res.status = 400;
        res.set_content(R"({"status":"error","msg":"invalid json"})", "application/json");
    }
}


void HttpServer::HandleDisconnect(const httplib::Request &req, httplib::Response &res) {
    try {
        // 1. 解析请求 JSON
        nlohmann::json jsonData = nlohmann::json::parse(req.body);

        // 2. 解析设备信息
        Device device;
        device.parseJson(jsonData);

        // 3. 调用调度器接口删除设备
        bool ok = Docker_scheduler::Disconnect_device(device);

        // 4. 根据删除结果返回 HTTP 响应
        if (ok) {
            res.status = 200;
            res.set_content(R"({"status":"ok","msg":"device removed success"})", "application/json");
        } else {
            res.status = 404;
            res.set_content(R"({"status":"error","msg":"device not found"})", "application/json");
        }

    } catch (const std::exception &e) {
        spdlog::error("HandleDisconnect exception: {}", e.what());
        res.status = 400;
        res.set_content(R"({"status":"error","msg":"invalid json or internal error"})", "application/json");
    }
}
