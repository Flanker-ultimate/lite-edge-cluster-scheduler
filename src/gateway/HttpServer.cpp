#include "HttpServer.h"
#include "httplib.h"
#include <algorithm>
#include <spdlog/spdlog.h>
#include <random>
#include <sstream>
#include <cstring>
#include <unordered_map>
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>
#include <utility>
#include "DockerClient.h"
#include "TimeRecorder.h"
#include <filesystem>
using json = nlohmann::json;
Args HttpServer::args; 
std::thread HttpServer::health_check_thread_;
std::atomic<bool> HttpServer::health_check_stop_{false};
HttpServer::HttpServer(std::string ip, const int port, const Args &out_args) : ip(std::move(ip)), port(port) {
    args = out_args;
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
    svr.Post(TASK_COMPLETED_ROUTE, this->HandleTaskCompleted);

    spdlog::info("HttpServer started success，ip:{} port:{}",this->ip, this->port);
    StartHealthCheckThread();
    auto result = svr.listen(this->ip, this->port);
    if (!result) {
        spdlog::error("HttpServer start failed!");
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
    // TimeRecord<std::chrono::milliseconds> time_record("HandleQuest");
    // time_record.startRecord();

    // TimeRecord<std::chrono::milliseconds> time_record_schedule("schedule");
    // time_record_schedule.startRecord();

    // // parse params
    // auto task_type_str = req.get_param_value("taskid");
    // auto real_url_param = req.get_param_value("real_url");
    // TaskType task_type;
    // // validate params
    // try {
    //     task_type = StrToTaskType(task_type_str);
    // } catch (const std::invalid_argument &e) {
    //     res.status = 400; // Bad Request
    //     res.set_content("invalid format taskid", "text/plain");
    //     return;
    // }
    // if (task_type == TaskType::Unknown) {
    //     res.status = 400; // Bad Request
    //     res.set_content("Missing taskid or real_url parameter", "text/plain");
    //     return;
    // }
    // if (real_url_param.empty()) {
    //     res.status = 400; // Bad Request
    //     res.set_content("Missing taskid or real_url parameter", "text/plain");
    //     return;
    // }
    // // get target_device_id
    // optional<SrvInfo> srv_info_opt = Docker_scheduler::getOrCrtSrvByTType(task_type);
    // if (srv_info_opt == nullopt) {
    //     res.status = 400; // Bad Request
    //     res.set_content("we can't get a useful srv", "text/plain");
    //     return;
    // }
    // // print in the end of quest
    // time_record_schedule.endRecord();

    // SrvInfo srv_info = srv_info_opt.value();
    // string origin_host_ip = req.remote_addr;
    // int origin_host_port = req.remote_port;
    // string transfer_host_ip = req.local_addr;
    // int transfer_host_port = req.local_port;
    // string tgt_host_ip = srv_info.ip;
    // int tgt_host_port = srv_info.port;

    // httplib::Client cli(tgt_host_ip, tgt_host_port);
    // // Forward the request to the target host.
    // httplib::MultipartFormDataItems items;
    // for (auto file: req.files) {
    //     items.push_back(file.second);
    // }

    // httplib::Result response;
    // try {
    //     response = cli.Post("/" + real_url_param, items);
    // } catch (const std::exception &e) {
    //     spdlog::error("{} Error sending request: {}", req.path, e.what());
    //     res.status = 500;
    //     res.set_content("Internal Server Error", "text/plain");
    //     return;
    // }

    // if (response != nullptr && response->status != -1) {
    //     // return to client
    //     res.status = response->status;
    //     res.set_header("Content-Type", response->get_header_value("Content-Type"));

    //     time_record.endRecord();
    //     // append gateway_time to response

    //     spdlog::info(
    //         "URL: {},task_type_str:{}, real_url_param:{} origin_host_ip: {}:{}, transfer_host_ip: {}:{}, tgt_host_ip: {}:{}, duration_time:{}, time_record_schedule:{}",
    //         req.path,
    //         task_type_str,
    //         real_url_param,
    //         origin_host_ip,
    //         origin_host_port,
    //         transfer_host_ip,
    //         transfer_host_port,
    //         tgt_host_ip,
    //         tgt_host_port,
    //         time_record.getDuration(),
    //         time_record_schedule.getDuration()
    //     );
    //     nlohmann::json jsonData = nlohmann::json::parse(response->body);
    //     jsonData["gateway_time"] = (double) (time_record.getDuration());
    //     res.body = jsonData.dump();
    // } else {
    //     res.status = 502;
    //     time_record.endRecord();
    //     res.set_content("Bad Gateway", "text/plain");
    //     spdlog::error("URL: {},task_type_str:{}, real_url_param:{} origin_host_ip: {}:{}, transfer_host_ip: {}:{}, tgt_host_ip: {}:{}, duration_time:{}, Error:{}",
    //         req.path,
    //         task_type_str,
    //         real_url_param,
    //         origin_host_ip,
    //         origin_host_port,
    //         transfer_host_ip,
    //         transfer_host_port,
    //         tgt_host_ip,
    //         tgt_host_port,
    //         time_record.getDuration(),
    //         "Connection failed or response = nullptr"
    //     );
    // }
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
    // 获取当前源文件所在目录
    std::string current_file = __FILE__;
    fs::path source_dir = fs::path(current_file).parent_path();
    fs::path project_root = source_dir.parent_path().parent_path();
    try {
        TimeRecord<std::chrono::milliseconds> time_record_schedule("schedule");
        time_record_schedule.startRecord();
        auto body_json = nlohmann::json::parse(req.body);

        // 检查必要字段：ip, tasktype, filename(s)
        if(!body_json.contains("ip") || !body_json.contains("tasktype")) {
            res.status = 400;
            res.set_content(R"({"status":"error","msg":"missing required fields"})", "application/json");
            return;
        }
        std::string ip = body_json["ip"].get<std::string>();
        TaskType tasktype = StrToTaskType(body_json["tasktype"].get<std::string>());
        std::vector<std::string> filenames;
        if (body_json.contains("filenames") && body_json["filenames"].is_array()) {
            for (const auto &item : body_json["filenames"]) {
                if (item.is_string()) {
                    filenames.push_back(item.get<std::string>());
                }
            }
        } else if (body_json.contains("filename") && body_json["filename"].is_string()) {
            filenames.push_back(body_json["filename"].get<std::string>());
        }
        if (filenames.empty()) {
            res.status = 400;
            res.set_content(R"json({"status":"error","msg":"missing filename(s)"})json", "application/json");
            return;
        }
        std::string req_id = body_json.value("req_id", "");
        int total_num = body_json.value("total_num", static_cast<int>(filenames.size()));
        if (total_num <= 0 || total_num != static_cast<int>(filenames.size())) {
            res.status = 400;
            res.set_content(R"({"status":"error","msg":"invalid total_num or filenames size mismatch"})", "application/json");
            return;
        }
        if (req_id.empty()) {
            req_id = filenames.front();
        }

        // 解析调度策略参数
        auto strategy_param = req.get_param_value("stargety");
        ScheduleStrategy strategy = ScheduleStrategy::LOAD_BASED; // 默认使用负载贪心策略

        if (!strategy_param.empty()) {
            if (strategy_param == "roundrobin" || strategy_param == "round_robin") {
                strategy = ScheduleStrategy::ROUND_ROBIN;
            } else if (strategy_param == "load" || strategy_param == "负载贪心") {
                strategy = ScheduleStrategy::LOAD_BASED;
            } else {
                res.status = 400;
                res.set_content(R"({"status":"error","msg":"invalid stargety parameter"})", "application/json");
                return;
            }
        }

        std::vector<ImageTask> tasks;
        tasks.reserve(filenames.size());
        for (const auto &filename : filenames) {
            fs::path fsfullpath = project_root / args.task_path / ip / filename;
            ImageTask task;
            task.task_id = filename;
            task.file_path = fsfullpath.string();
            task.client_ip = ip;
            task.task_type = tasktype;
            task.schedule_strategy = strategy;
            task.req_id = req_id;
            tasks.push_back(task);
        }

        ClientRequest client_req;
        client_req.req_id = req_id;
        client_req.client_ip = ip;
        client_req.task_type = tasktype;
        client_req.schedule_strategy = strategy;
        client_req.total_num = total_num;
        client_req.enqueue_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        client_req.tasks = std::move(tasks);

        Docker_scheduler::SubmitClientRequest(client_req);

        spdlog::info("client_req {} enqueued: {} tasks, strategy={}", req_id, total_num,
                     strategy == ScheduleStrategy::ROUND_ROBIN ? "round-robin" : "load-based");
        res.status = 202;
        res.set_content(R"({"status":"queued","msg":"task enqueued"})", "application/json");


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

void HttpServer::HandleTaskCompleted(const httplib::Request &req, httplib::Response &res) {
    try {
        auto jsonData = json::parse(req.body);

        // 解析必填字段
        if (!jsonData.contains("task_id") || !jsonData.contains("device_id") ||
            !jsonData.contains("client_ip") || !jsonData.contains("status")) {
            res.status = 400;
            res.set_content("{\"status\":\"error\",\"msg\":\"missing required fields: task_id, device_id, client_ip, status\"}", "application/json");
            return;
        }

        std::string task_id = jsonData["task_id"];
        std::string device_id = jsonData["device_id"];
        std::string client_ip = jsonData["client_ip"];
        std::string status = jsonData["status"];

        // 如果状态是成功，则从运行中的任务列表中移除
        if (status == "success") {
            auto completed_task = Docker_scheduler::GetTaskQueueManager().CompleteTaskAndGet(task_id);
            if (!completed_task.has_value()) {
                // 可能已经因为服务迁移/重试而不在运行队列，这里返回200保证幂等
                spdlog::warn("Task completion received but not found in running queue: reported_task_id={}", task_id);
                res.status = 200;
                res.set_content("{\"status\":\"ok\",\"msg\":\"task not found (maybe already migrated)\"}", "application/json");
                return;
            }

            const auto &task = completed_task.value();
            spdlog::info("Task {} completed successfully on device {} for client {}",
                         task.task_id, device_id, task.client_ip);

            if (!args.keep_upload) {
                namespace fs = std::filesystem;
                std::string current_file = __FILE__;
                fs::path source_dir = fs::path(current_file).parent_path();
                fs::path project_root = source_dir.parent_path().parent_path();
                fs::path upload_path = project_root / args.task_path / task.client_ip / task.task_id;

                std::error_code ec;
                const bool removed = fs::remove(upload_path, ec);
                if (ec) {
                    spdlog::warn("Failed to delete uploaded file after completion: path={} err={}",
                                 upload_path.string(), ec.message());
                } else if (removed) {
                    spdlog::info("Deleted uploaded file after completion: path={}", upload_path.string());
                } else {
                    spdlog::debug("Uploaded file already missing (skip delete): path={}", upload_path.string());
                }
            }

            res.status = 200;
            res.set_content("{\"status\":\"ok\",\"msg\":\"task marked as completed\"}", "application/json");
            return;
        } else {
            // 处理失败情况，可以选择重试或移入失败列表
            spdlog::warn("Task {} failed on device {} for client {}: status={}",
                       task_id, device_id, client_ip, status);
            res.status = 200;
            res.set_content("{\"status\":\"ok\",\"msg\":\"task failure acknowledged\"}", "application/json");
        }

    } catch (const std::exception &e) {
        spdlog::error("HandleTaskCompleted exception: {}", e.what());
        res.status = 400;
        res.set_content("{\"status\":\"error\",\"msg\":\"invalid json\"}", "application/json");
    }
}

void HttpServer::StartHealthCheckThread() {
    // Start only once; the server runs for the lifetime of the process.
    static std::atomic<bool> started{false};
    bool expected = false;
    if (!started.compare_exchange_strong(expected, true)) {
        return;
    }

    health_check_stop_.store(false);
    health_check_thread_ = std::thread(&HttpServer::HealthCheckLoop, this);
    health_check_thread_.detach();
    spdlog::info("Gateway health-check thread started (interval_ms={}, latency_threshold_sec={})",
                 HEALTH_CHECK_INTERVAL, HEALTH_CHECK_LATENCY_THRESHOLD);
}

void HttpServer::HealthCheckLoop() {
    using Clock = std::chrono::steady_clock;

    struct UuidHasher {
        size_t operator()(const DeviceID &id) const noexcept {
            const uint8_t *p = id.data;
            size_t h = 1469598103934665603ull;
            for (size_t i = 0; i < 16; ++i) {
                h ^= static_cast<size_t>(p[i]);
                h *= 1099511628211ull;
            }
            return h;
        }
    };
    std::unordered_map<DeviceID, Clock::time_point, UuidHasher> last_recover;

    while (!health_check_stop_.load()) {
        std::vector<DeviceID> to_recover;
        {
            std::shared_lock<std::shared_mutex> lock(Docker_scheduler::getDeviceMutex());
            auto &device_status = Docker_scheduler::getDeviceStatus();
            const auto now = Clock::now();

            for (const auto &[dev_id, status] : device_status) {
                const double latency_sec = status.net_latency / 1000.0; // agent reports ms
                if (latency_sec <= HEALTH_CHECK_LATENCY_THRESHOLD) {
                    continue;
                }

                const auto it = last_recover.find(dev_id);
                if (it != last_recover.end()) {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second).count();
                    if (elapsed < HEALTH_CHECK_COOLDOWN_SEC) {
                        continue;
                    }
                }

                last_recover[dev_id] = now;
                to_recover.push_back(dev_id);
            }
        }

        for (const auto &dev_id : to_recover) {
            spdlog::warn("HealthCheck: latency > {}s, trigger service migration (recover running tasks), device_id={}",
                         HEALTH_CHECK_LATENCY_THRESHOLD, boost::uuids::to_string(dev_id));
            Docker_scheduler::GetTaskQueueManager().RecoverTasks(dev_id);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(HEALTH_CHECK_INTERVAL));
    }
}
