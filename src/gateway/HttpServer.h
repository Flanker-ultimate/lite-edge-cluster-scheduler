#pragma once

#include <string>
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
    std::string ip;
    int port;
    static Args args;
};
