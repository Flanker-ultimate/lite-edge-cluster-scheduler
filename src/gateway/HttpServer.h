#pragma once

#include <string>
#include <thread>
#include <atomic>
#include <cstdint>
#include <httplib.h>
#include "scheduler.h"
#include "config.h"
#include "spdlog/spdlog.h"

// 用户请求ai任务处理
const std::string QUSET_ROUTE = "/quest";  // dropped,ignore
const std::string REGISTER_NODE_ROUTE = "/register_node";
const std::string HOTSTART_ROUTE = "/hot_start";
const std::string SCHEDULE_ROUTE = "/schedule";
const std::string DISCONNECT_NODE_ROUTE = "/unregister_node";
const std::string TASK_COMPLETED_ROUTE = "/task_completed";
const std::string TASK_RESULT_READY_ROUTE = "/task_result_ready";
const std::string REQ_LIST_ROUTE = "/reqs";
const std::string REQ_DETAIL_ROUTE = "/req";
const std::string SUB_REQ_ROUTE = "/sub_req";
const std::string NODES_ROUTE = "/nodes";

class HttpServer {
public:
    HttpServer(std::string ip, int port, const Args &args);
    bool Start();

private:
    static void HandleQuest(const httplib::Request &req, httplib::Response &res);
    static void HandleRegisterNode(const httplib::Request &req, httplib::Response &res);
    static void HandleHotStart(const httplib::Request &req, httplib::Response &res);
    static void HandleSchedule(const httplib::Request &req, httplib::Response &res);
    static void HandleDisconnect(const httplib::Request &req, httplib::Response &res);
    static void HandleTaskCompleted(const httplib::Request &req, httplib::Response &res);
    static void HandleTaskResultReady(const httplib::Request &req, httplib::Response &res);
    static void HandleReqList(const httplib::Request &req, httplib::Response &res);
    static void HandleReqDetail(const httplib::Request &req, httplib::Response &res);
    static void HandleSubReq(const httplib::Request &req, httplib::Response &res);
    static void HandleNodes(const httplib::Request &req, httplib::Response &res);

    void StartHealthCheckThread();
    void HealthCheckLoop();

    std::string ip;
    int port;
    static Args args;
    static std::thread health_check_thread_;
    static std::atomic<bool> health_check_stop_;
    static constexpr double HEALTH_CHECK_LATENCY_THRESHOLD = 10.0;  // 10秒延迟阈值
    static constexpr uint32_t HEALTH_CHECK_INTERVAL = 5000;         // 5秒检查间隔
    static constexpr uint32_t HEALTH_CHECK_COOLDOWN_SEC = 30;
};
