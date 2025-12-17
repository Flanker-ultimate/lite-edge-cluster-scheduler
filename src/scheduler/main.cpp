#include "scheduler.h"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

int main() {
    const std::string bind_ip = "0.0.0.0";
    const int bind_port = 7000;

    httplib::Server svr;
    svr.Post("/task/report", [](const httplib::Request &req, httplib::Response &res) {
        try {
            auto body_json = nlohmann::json::parse(req.body);
            const std::string filename = body_json.value("filename", "");
            if (filename.empty()) {
                res.status = 400;
                res.set_content(R"({"status":"error","msg":"filename missing"})", "application/json");
                return;
            }
            bool ok = Docker_scheduler::CompleteTask(filename);
            if (ok) {
                res.status = 200;
                res.set_content(R"({"status":"ok"})", "application/json");
            } else {
                res.status = 404;
                res.set_content(R"({"status":"not_found"})", "application/json");
            }
        } catch (const std::exception &e) {
            spdlog::error("Task report parse error: {}", e.what());
            res.status = 400;
            res.set_content(R"({"status":"error","msg":"invalid json"})", "application/json");
        }
    });

    Docker_scheduler::StartSchedulerLoop();
    spdlog::info("Task report server listening on {}:{}", bind_ip, bind_port);
    svr.listen(bind_ip, bind_port);
    return 0;
}
