#include "MachineInfoCollector.h"
#include <httplib.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <nlohmann/json.hpp>
#include "device_type.h"
#include "device.h"
#include <thread>
#include <chrono>
#include <atomic>
#include <random>
#include <cstdlib>
#include <string>
#include <sstream>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <filesystem>
#include <algorithm>
#include <csignal>

// 将 const char* 硬编码改为全局变量
// 默认值设为 127.0.0.1，方便本地测试。生产环境建议通过参数覆盖。
std::string g_gateway_ip = "127.0.0.1";
int g_gateway_port = 6666;

const int kAgentPort = 8000;

using json = nlohmann::json;
using namespace httplib;
using namespace std::chrono_literals;

// 全局原子变量标记是否运行（用于线程安全退出）
std::atomic<bool> g_is_running(true);
std::atomic<bool> g_manage_services(true);

// 后端服务管理（由 agent 统一启动/守护）
static std::mutex g_backend_mu;
static std::unordered_set<std::string> g_running_backends;
static json g_slave_backend_cfg = json::object();
static int g_restart_delay_sec = 2;
static std::string g_project_root = ".";
static bool g_allow_remote_control = false;
static std::string g_slave_log_dir = "workspace/slave/log";

// 进程生命周期：agent 退出时需要关闭 recv_server/rst_send/后端进程
static std::mutex g_proc_mu;
#ifdef _WIN32
static std::unordered_map<std::string, int> g_managed_pids; // unused on Windows
#else
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
static std::unordered_map<std::string, pid_t> g_managed_pgids; // name -> process group id
#endif

// 随机数生成器（用于带宽波动，范围调整为50-500Mbps）
static std::random_device rd;
static std::mt19937 gen(rd());
static std::uniform_real_distribution<> bandwidth_dist(50.0, 500.0); // 50-500Mbps的波动范围

static std::string BuildResult(const std::string &status, const json &v) {
    json j;
    j["status"] = status;
    j["result"] = v;
    return j.dump();
}

static std::string BuildSuccess(const json &v) { return BuildResult("success", v); }

static std::string BuildFailed(const json &v) { return BuildResult("failed", v); }

static std::string FindProjectRoot() {
    try {
        auto cur = std::filesystem::current_path();
        for (int i = 0; i < 6; ++i) {
            auto cmake = cur / "CMakeLists.txt";
            auto cfg = cur / "config_files";
            if (std::filesystem::exists(cmake) && std::filesystem::is_directory(cfg)) {
                return cur.string();
            }
            if (!cur.has_parent_path()) break;
            cur = cur.parent_path();
        }
    } catch (...) {
    }
    return ".";
}

static std::string QuoteArg(const std::string &s) {
#ifdef _WIN32
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    out += "\"";
    return out;
#else
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "'\\''";
        else out += c;
    }
    out += "'";
    return out;
#endif
}

static void StopAllManagedChildren() {
#ifdef _WIN32
    // Windows: best-effort only (agent doesn't own children reliably when started via system())
    return;
#else
    std::lock_guard<std::mutex> lock(g_proc_mu);
    for (const auto &kv : g_managed_pgids) {
        pid_t pgid = kv.second;
        if (pgid <= 0) continue;
        // kill entire process group
        kill(-pgid, SIGTERM);
    }
#endif
}

static void HandleSignal(int) {
    g_is_running.store(false);
}

static void InstallSignalHandlers() {
#ifndef _WIN32
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
#endif
}

static void ManagedSystemLoop(const std::string &name, const std::string &cmd, int restart_delay_sec) {
#ifdef _WIN32
    while (g_is_running.load()) {
        spdlog::info("[agent] starting {}: {}", name, cmd);
        int rc = std::system(cmd.c_str());
        if (!g_is_running.load()) {
            break;
        }
        spdlog::warn("[agent] {} exited with code {}, restarting in {}s", name, rc, restart_delay_sec);
        std::this_thread::sleep_for(std::chrono::seconds(restart_delay_sec));
    }
#else
    while (g_is_running.load()) {
        spdlog::info("[agent] starting {}: {}", name, cmd);

        pid_t pid = fork();
        if (pid == 0) {
            // child: create new process group so we can kill everything on shutdown
            setpgid(0, 0);
            execl("/bin/sh", "sh", "-c", cmd.c_str(), (char *)nullptr);
            _exit(127);
        }
        if (pid < 0) {
            spdlog::error("[agent] fork failed for {}, retry in {}s", name, restart_delay_sec);
            std::this_thread::sleep_for(std::chrono::seconds(restart_delay_sec));
            continue;
        }

        // parent: record pgid
        pid_t pgid = pid;
        setpgid(pid, pgid);
        {
            std::lock_guard<std::mutex> lock(g_proc_mu);
            g_managed_pgids[name] = pgid;
        }

        int status = 0;
        while (true) {
            pid_t w = waitpid(pid, &status, 0);
            if (w == -1 && errno == EINTR) {
                if (!g_is_running.load()) {
                    break;
                }
                continue;
            }
            break;
        }

        {
            std::lock_guard<std::mutex> lock(g_proc_mu);
            auto it = g_managed_pgids.find(name);
            if (it != g_managed_pgids.end() && it->second == pgid) {
                g_managed_pgids.erase(it);
            }
        }

        if (!g_is_running.load()) {
            // ensure group is terminated
            kill(-pgid, SIGTERM);
            break;
        }

        int rc = 0;
        if (WIFEXITED(status)) {
            rc = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            rc = 128 + WTERMSIG(status);
        }
        spdlog::warn("[agent] {} exited with code {}, restarting in {}s", name, rc, restart_delay_sec);
        std::this_thread::sleep_for(std::chrono::seconds(restart_delay_sec));
    }
#endif
}

static std::string ReplaceAll(std::string s, const std::string &from, const std::string &to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static json LoadAgentServicesConfig() {
    const char *cfg_env = std::getenv("AGENT_SERVICES_CONFIG");
    const std::string cfg_path = (cfg_env && *cfg_env) ? cfg_env : "config_files/agent_services.json";
    std::ifstream f(cfg_path);
    if (!f.is_open()) {
        return json::object();
    }
    try {
        json j;
        f >> j;
        return j.is_object() ? j : json::object();
    } catch (...) {
        return json::object();
    }
}

static std::vector<std::string> GetAutostartServices(const json &cfg) {
    std::vector<std::string> out;
    if (!cfg.is_object()) return out;
    if (!cfg.contains("autostart_services") || !cfg["autostart_services"].is_array()) return out;
    for (const auto &v : cfg["autostart_services"]) {
        if (v.is_string()) {
            std::string s = v.get<std::string>();
            if (!s.empty()) out.push_back(std::move(s));
        }
    }
    return out;
}

static std::string JoinCsv(const std::vector<std::string> &items) {
    std::ostringstream oss;
    bool first = true;
    for (const auto &s : items) {
        if (s.empty()) continue;
        if (!first) oss << ",";
        oss << s;
        first = false;
    }
    return oss.str();
}

static std::string WithEnv(const std::string &cmd, const std::vector<std::pair<std::string, std::string>> &envs) {
    std::string prefix;
#ifdef _WIN32
    // cmd.exe: set VAR=... && set VAR2=... && <cmd>
    for (const auto &kv : envs) {
        prefix += "set " + kv.first + "=" + kv.second + " && ";
    }
    return prefix + cmd;
#else
    // /bin/sh: VAR=... VAR2=... <cmd>
    for (const auto &kv : envs) {
        prefix += kv.first + "=" + QuoteArg(kv.second) + " ";
    }
    return prefix + cmd;
#endif
}

static std::string ResolvePath(const std::string &root, const std::string &p) {
    if (p.empty()) return "";
    try {
        std::filesystem::path path(p);
        if (path.is_absolute()) return path.string();
        return std::filesystem::absolute(std::filesystem::path(root) / path).string();
    } catch (...) {
        return p;
    }
}

static std::string AppendRedirect(const std::string &cmd, const std::string &log_path) {
    if (log_path.empty()) return cmd;
    std::string quoted = QuoteArg(log_path);
#ifdef _WIN32
    return cmd + " >> " + quoted + " 2>&1";
#else
    return cmd + " >> " + quoted + " 2>&1";
#endif
}

static void SetupAgentLogging() {
    try {
        std::filesystem::create_directories(g_slave_log_dir);
        auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            (std::filesystem::path(g_slave_log_dir) / "agent.log").string(), true
        );
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        std::vector<spdlog::sink_ptr> sinks{file_sink, console_sink};
        auto logger = std::make_shared<spdlog::logger>("agent", sinks.begin(), sinks.end());
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    } catch (...) {
        // fallback to default stderr logger
    }
}

static json LoadSlaveBackendConfig() {
    const char *cfg_env = std::getenv("SLAVE_BACKEND_CONFIG");
    std::string cfg_path = (cfg_env && *cfg_env) ? cfg_env : (std::filesystem::path(g_project_root) / "config_files" / "slave_backend.json").string();
    std::ifstream f(cfg_path);
    if (!f.is_open()) {
        spdlog::warn("[agent] failed to open slave backend config: {}", cfg_path);
        return json::object();
    }
    try {
        json j;
        f >> j;
        if (!j.is_object()) return json::object();
        return j;
    } catch (const std::exception &e) {
        spdlog::warn("[agent] failed to parse slave backend config: {}", e.what());
        return json::object();
    }
}

static std::vector<std::string> GetAgentAutostartFromSlaveBackend(const json &cfg) {
    std::vector<std::string> out;
    if (!cfg.is_object()) return out;
    if (!cfg.contains("services") || !cfg["services"].is_object()) return out;
    for (auto it = cfg["services"].begin(); it != cfg["services"].end(); ++it) {
        const auto &name = it.key();
        const auto &entry = it.value();
        if (!entry.is_object()) continue;
        if (entry.contains("agent_autostart") && entry["agent_autostart"].is_boolean() && entry["agent_autostart"].get<bool>()) {
            out.push_back(name);
        }
    }
    return out;
}

static std::string DetectSlaveBackendConfigPath() {
    const char *cfg_env = std::getenv("SLAVE_BACKEND_CONFIG");
    std::string cfg_path = (cfg_env && *cfg_env) ? cfg_env : (std::filesystem::path(g_project_root) / "config_files" / "slave_backend.json").string();
    return cfg_path;
}

static std::vector<std::string> UniqueUnion(std::vector<std::string> a, const std::vector<std::string> &b) {
    std::unordered_set<std::string> seen;
    for (const auto &s : a) seen.insert(s);
    for (const auto &s : b) {
        if (seen.insert(s).second) a.push_back(s);
    }
    return a;
}

static json GetServiceEntry(const json &cfg, const std::string &service) {
    if (!cfg.is_object()) return json::object();
    if (!cfg.contains("services") || !cfg["services"].is_object()) return json::object();
    if (!cfg["services"].contains(service)) return json::object();
    const auto &v = cfg["services"][service];
    return v.is_object() ? v : json::object();
}

static bool EnsureBackendStarted(const std::string &service_name) {
    std::lock_guard<std::mutex> lock(g_backend_mu);
    if (g_running_backends.find(service_name) != g_running_backends.end()) {
        return true;
    }

    json entry = GetServiceEntry(g_slave_backend_cfg, service_name);
    if (!entry.is_object()) {
        spdlog::warn("[agent] ensure_service: unknown service={}", service_name);
        return false;
    }

    std::string backend = "local";
    if (entry.contains("backend") && entry["backend"].is_string()) {
        backend = entry["backend"].get<std::string>();
    }
    if (backend == "local") {
        return true;
    }

    std::string start_cmd;
    if (entry.contains("start_cmd") && entry["start_cmd"].is_string()) {
        start_cmd = entry["start_cmd"].get<std::string>();
    }
    if (start_cmd.empty()) {
        spdlog::warn("[agent] ensure_service: missing start_cmd, service={} backend={}", service_name, backend);
        return false;
    }

    std::string input_dir = "workspace/slave/data/input/" + service_name;
    std::string output_dir = "workspace/slave/data/output/" + service_name;
    if (entry.contains("input_dir") && entry["input_dir"].is_string()) input_dir = entry["input_dir"].get<std::string>();
    if (entry.contains("output_dir") && entry["output_dir"].is_string()) output_dir = entry["output_dir"].get<std::string>();

    input_dir = ResolvePath(g_project_root, input_dir);
    output_dir = ResolvePath(g_project_root, output_dir);

    try {
        std::filesystem::create_directories(input_dir);
        std::filesystem::create_directories(output_dir);
    } catch (...) {
    }

    start_cmd = ReplaceAll(start_cmd, "${INPUT_DIR}", input_dir);
    start_cmd = ReplaceAll(start_cmd, "${OUTPUT_DIR}", output_dir);
    start_cmd = ReplaceAll(start_cmd, "${SERVICE_NAME}", service_name);

    std::vector<std::pair<std::string, std::string>> envs = {
        {"PYTHONUNBUFFERED", "1"},
        {"SERVICE_NAME", service_name},
        {"INPUT_DIR", input_dir},
        {"OUTPUT_DIR", output_dir},
    };
    std::string cmd = WithEnv(start_cmd, envs);

    const std::string svc_log_dir = (std::filesystem::path(g_slave_log_dir) / service_name).string();
    try {
        std::filesystem::create_directories(svc_log_dir);
    } catch (...) {
    }
    const std::string svc_log_path = (std::filesystem::path(svc_log_dir) / "service.log").string();
    cmd = AppendRedirect(cmd, svc_log_path);

    std::thread(ManagedSystemLoop, "backend_" + service_name, cmd, g_restart_delay_sec).detach();
    g_running_backends.insert(service_name);
    spdlog::info("[agent] backend started (managed) service={} backend={}", service_name, backend);
    return true;
}

static std::vector<std::string> GetRunningBackendsSnapshot() {
    std::lock_guard<std::mutex> lock(g_backend_mu);
    std::vector<std::string> out;
    out.reserve(g_running_backends.size());
    for (const auto &s : g_running_backends) out.push_back(s);
    std::sort(out.begin(), out.end());
    return out;
}

static void StartSlaveServices(const std::string &device_id) {
    json cfg = LoadAgentServicesConfig();
    const auto autostart_services_cfg = GetAutostartServices(cfg);

    const char *py_env = std::getenv("PYTHON_BIN");
    std::string python_bin = py_env && *py_env ? py_env : "python3";
    if (cfg.contains("python_bin") && cfg["python_bin"].is_string()) {
        python_bin = cfg["python_bin"].get<std::string>();
    }

    int restart_delay_sec = 2;
    if (cfg.contains("restart_delay_sec") && cfg["restart_delay_sec"].is_number_integer()) {
        restart_delay_sec = cfg["restart_delay_sec"].get<int>();
        if (restart_delay_sec < 0) restart_delay_sec = 0;
    }
    g_restart_delay_sec = restart_delay_sec;

    std::string recv_server_cmd;
    if (cfg.contains("recv_server_cmd") && cfg["recv_server_cmd"].is_string()) {
        recv_server_cmd = cfg["recv_server_cmd"].get<std::string>();
    } else {
        recv_server_cmd = "{PYTHON} src/modules/slave/recv_server.py";
    }

    std::string rst_send_cmd;
    if (cfg.contains("rst_send_cmd") && cfg["rst_send_cmd"].is_string()) {
        rst_send_cmd = cfg["rst_send_cmd"].get<std::string>();
    } else {
        rst_send_cmd =
            "{PYTHON} src/modules/slave/rst_send.py --config config_files/slave_backend.json "
            "--input-dir workspace/slave/data/output --interval 5 --target-port 8888 "
            "--gateway-host {MASTER_IP} --gateway-port {MASTER_PORT} --device-id {DEVICE_ID}";
    }

    recv_server_cmd = ReplaceAll(recv_server_cmd, "{PYTHON}", python_bin);
    rst_send_cmd = ReplaceAll(rst_send_cmd, "{PYTHON}", python_bin);

    recv_server_cmd = ReplaceAll(recv_server_cmd, "{DEVICE_ID}", device_id);
    rst_send_cmd = ReplaceAll(rst_send_cmd, "{DEVICE_ID}", device_id);

    recv_server_cmd = ReplaceAll(recv_server_cmd, "{MASTER_IP}", g_gateway_ip);
    rst_send_cmd = ReplaceAll(rst_send_cmd, "{MASTER_IP}", g_gateway_ip);

    recv_server_cmd = ReplaceAll(recv_server_cmd, "{MASTER_PORT}", std::to_string(g_gateway_port));
    rst_send_cmd = ReplaceAll(rst_send_cmd, "{MASTER_PORT}", std::to_string(g_gateway_port));

    spdlog::info("[agent] manage services with restart_delay_sec={}", restart_delay_sec);
    spdlog::info("[agent] log_dir={}", g_slave_log_dir);
    spdlog::info("[agent] slave_backend_config={}", DetectSlaveBackendConfigPath());
    if (!autostart_services_cfg.empty()) {
        spdlog::info("[agent] autostart_services(from agent_services.json)={}", JoinCsv(autostart_services_cfg));
    } else {
        spdlog::info("[agent] autostart_services(from agent_services.json)=<empty>");
    }
    spdlog::info("[agent] recv_server_cmd={}", recv_server_cmd);
    spdlog::info("[agent] rst_send_cmd={}", rst_send_cmd);
    std::vector<std::pair<std::string, std::string>> recv_envs = {
        {"SLAVE_BACKEND_CONFIG", "config_files/slave_backend.json"},
    };

    const std::string recv_log_path = (std::filesystem::path(g_slave_log_dir) / "recv_server.log").string();
    const std::string rst_log_path = (std::filesystem::path(g_slave_log_dir) / "rst_send.log").string();

    std::thread(ManagedSystemLoop, "recv_server", AppendRedirect(WithEnv(recv_server_cmd, recv_envs), recv_log_path), restart_delay_sec).detach();
    std::thread(ManagedSystemLoop, "rst_send", AppendRedirect(rst_send_cmd, rst_log_path), restart_delay_sec).detach();

    // 统一由 agent 启动后端服务：autostart_services(agent_services.json) + agent_autostart(slave_backend.json)
    {
        std::lock_guard<std::mutex> lock(g_backend_mu);
        g_slave_backend_cfg = LoadSlaveBackendConfig();
    }
    const auto autostart_services_backend = GetAgentAutostartFromSlaveBackend(g_slave_backend_cfg);
    const auto autostart_services = UniqueUnion(autostart_services_cfg, autostart_services_backend);
    if (!autostart_services_backend.empty()) {
        spdlog::info("[agent] agent_autostart(from slave_backend.json)={}", JoinCsv(autostart_services_backend));
    }
    if (!autostart_services.empty()) {
        spdlog::info("[agent] ensure backends at startup={}", JoinCsv(autostart_services));
    }
    for (const auto &svc : autostart_services) {
        EnsureBackendStarted(svc);
    }
}

static bool RegisterNode(MachineInfoCollector &collector) {
    try {
        // --- 修改点 2: 使用全局变量 g_gateway_ip 和 g_gateway_port ---
        Client client(g_gateway_ip.c_str(), g_gateway_port);

        json cfg = LoadAgentServicesConfig();
        const auto autostart_services_cfg = GetAutostartServices(cfg);
        {
            std::lock_guard<std::mutex> lock(g_backend_mu);
            if (g_slave_backend_cfg.empty()) {
                g_slave_backend_cfg = LoadSlaveBackendConfig();
            }
        }
        const auto autostart_services_backend = GetAgentAutostartFromSlaveBackend(g_slave_backend_cfg);
        const auto autostart_services = UniqueUnion(autostart_services_cfg, autostart_services_backend);

        json j = {
                {"type",       AGENT_DEVICE_TYPE},
                {"global_id",  collector.GetGlobalId()},
                {"ip_address", collector.GetIp()},
                {"agent_port", kAgentPort},
                {"services",   autostart_services},
        };

        Result result = client.Post("/register_node", j.dump(), "application/json");
        if (!result || result->status != OK_200) {
            spdlog::error("Failed to register node: {}", httplib::to_string(result.error()));
            return false;
        }

        spdlog::info("Node registered successfully");
        return true;
    } catch (const std::exception &e) {
        spdlog::error("Failed to register node: {}", e.what());
        return false;
    }
}

static bool DisconnectNode(MachineInfoCollector &collector) {
    try {
        // --- 修改点 3: 使用全局变量 ---
        Client client(g_gateway_ip.c_str(), g_gateway_port);

        json j = {
                {"type",       AGENT_DEVICE_TYPE},
                {"global_id",  collector.GetGlobalId()},
                {"ip_address", collector.GetIp()},
                {"agent_port", kAgentPort},
        };

        Result result = client.Post("/unregister_node", j.dump(), "application/json");
        if (!result || result->status != OK_200) {
            spdlog::error("Failed to disconnect node: {}", httplib::to_string(result.error()));
            return false;
        }

        spdlog::info("Node disconnected successfully");
        return true;
    } catch (const std::exception &e) {
        spdlog::error("Failed to disconnect node: {}", e.what());
        return false;
    }
}

// 后台定时线程：处理自动断开和重连（支持禁用自动断开）
static void AutoConnectThread(MachineInfoCollector &collector, int disconnect_sec, int reconnect_sec) {
    // 如果disconnect_sec <= 0，直接禁用自动断开重连功能
    if (disconnect_sec <= 0) {
        spdlog::info("Auto-disconnect is disabled (disconnect time <= 0)");
        // 线程进入等待状态，直到程序退出
        while (g_is_running) {
            std::this_thread::sleep_for(1s);
        }
        return;
    }

    // 正常执行自动断开重连逻辑
    while (g_is_running) {
        // 步骤1：等待disconnect_sec秒后断开连接
        spdlog::info("Waiting {}s to disconnect...", disconnect_sec);
        for (int i = 0; i < disconnect_sec && g_is_running; ++i) {
            std::this_thread::sleep_for(1s); // 每秒检查一次是否需要退出
        }
        if (!g_is_running) break;

        // 发送断开连接请求
        DisconnectNode(collector);

        // 步骤2：等待reconnect_sec秒后重新注册
        spdlog::info("Waiting {}s to reconnect...", reconnect_sec);
        for (int i = 0; i < reconnect_sec && g_is_running; ++i) {
            std::this_thread::sleep_for(1s);
        }
        if (!g_is_running) break;

        // 重新注册
        RegisterNode(collector);
    }
}

// 打印帮助信息
static void PrintHelp(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --master-ip <ip>         Set Master/Gateway IP (env: MASTER_IP, default: 127.0.0.1)\n"
              << "  --master-port <port>     Set Master/Gateway Port (env: MASTER_PORT, default: 6666)\n"
              << "  --disconnect <seconds>   Set auto-disconnect time (default: 30s, <=0 to disable)\n"
              << "  --reconnect <seconds>    Set auto-reconnect time (default: 20s)\n"
              << "  --bandwidth-fluctuate    Enable network bandwidth fluctuation simulation (50-500Mbps)\n"
              << "  --no-manage-services     Do not start/manage recv_server & rst_send\n"
              << "                           (env: PYTHON_BIN overrides python executable)\n"
              << "                           (env: AGENT_SERVICES_CONFIG overrides config path)\n"
              << "                           (env: SLAVE_BACKEND_CONFIG overrides backend config)\n"
              << "                           (env: AGENT_ALLOW_REMOTE_CONTROL=1 allows remote ensure_service)\n"
              << "  --help                   Show this help message\n" << std::endl;
}

int main(int argc, char* argv[]) {
    // --- 修改点 4: 优先读取环境变量 ---
    const char* env_ip = std::getenv("MASTER_IP");
    if (env_ip) {
        g_gateway_ip = env_ip;
    }
    const char* env_port = std::getenv("MASTER_PORT");
    if (env_port) {
        try {
            g_gateway_port = std::stoi(env_port);
        } catch (...) {
            spdlog::warn("Invalid MASTER_PORT env var, using default: {}", g_gateway_port);
        }
    }

    g_project_root = FindProjectRoot();
    const char *allow_remote = std::getenv("AGENT_ALLOW_REMOTE_CONTROL");
    g_allow_remote_control = (allow_remote && std::string(allow_remote) == "1");
    g_slave_log_dir = ResolvePath(g_project_root, "workspace/slave/log");
    SetupAgentLogging();
    InstallSignalHandlers();

    // 默认参数设置
    int disconnect_sec = 30;    // 默认断连时间30秒
    int reconnect_sec = 20;     // 默认重连时间20秒
    bool bandwidth_fluctuate = false;  // 默认不开启带宽波动

    // 解析命令行参数（允许disconnect_sec <=0）
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--disconnect" && i + 1 < argc) {
            try {
                disconnect_sec = std::stoi(argv[++i]);
                if (disconnect_sec < 0) {
                    spdlog::info("Auto-disconnect will be disabled (negative value provided)");
                }
            } catch (const std::exception& e) {
                spdlog::error("Invalid disconnect time: {}", e.what());
                PrintHelp(argv[0]);
                return 1;
            }
        } else if (arg == "--reconnect" && i + 1 < argc) {
            try {
                reconnect_sec = std::stoi(argv[++i]);
                if (reconnect_sec <= 0) throw std::invalid_argument("must be positive");
            } catch (const std::exception& e) {
                spdlog::error("Invalid reconnect time: {}", e.what());
                PrintHelp(argv[0]);
                return 1;
            }
        } else if (arg == "--bandwidth-fluctuate") {
            bandwidth_fluctuate = true;
            spdlog::info("Bandwidth fluctuation enabled (range: 50-500Mbps)");
        }
        else if (arg == "--no-manage-services") {
            g_manage_services.store(false);
            spdlog::info("Disable managing slave services (recv_server/rst_send)");
        }

        else if (arg == "--master-ip" && i + 1 < argc) {
            g_gateway_ip = argv[++i];
        } else if (arg == "--master-port" && i + 1 < argc) {
            try {
                g_gateway_port = std::stoi(argv[++i]);
            } catch (const std::exception& e) {
                spdlog::error("Invalid master port: {}", e.what());
                return 1;
            }
        }
        else if (arg == "--help") {
            PrintHelp(argv[0]);
            return 0;
        } else {
            spdlog::error("Unknown argument: {}", arg);
            PrintHelp(argv[0]);
            return 1;
        }
    }

    // 打印参数配置
    spdlog::info("===== Agent Configuration =====");
    spdlog::info("Master IP: {}", g_gateway_ip);
    spdlog::info("Master Port: {}", g_gateway_port);
    if (disconnect_sec <= 0) {
        spdlog::info("Auto-disconnect: Disabled");
    } else {
        spdlog::info("Auto-disconnect time: {}s", disconnect_sec);
    }
    spdlog::info("Auto-reconnect time: {}s", reconnect_sec);
    spdlog::info("Bandwidth fluctuation: {}", (bandwidth_fluctuate ? "Enabled (50-500Mbps)" : "Disabled"));
    spdlog::info("===============================\n");

    // 使用动态地址初始化 MachineInfoCollector
    MachineInfoCollector collector(g_gateway_ip, g_gateway_port);
    httplib::Server server;
    // 允许在收到信号后优雅退出 listen，从而走到清理逻辑
    std::thread([&server]() {
        while (g_is_running.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        try {
            server.stop();
        } catch (...) {
        }
    }).detach();

    // 初始注册节点
    if (!RegisterNode(collector)) {
        spdlog::error("Initial registration failed. Exiting.");
        return 1;
    }

    // 启动后台自动断开重连线程
    if (g_manage_services.load()) {
        StartSlaveServices(collector.GetGlobalId());
    }

    std::thread auto_connect_thread(AutoConnectThread, std::ref(collector), disconnect_sec, reconnect_sec);

    // 异常处理
    server.set_exception_handler([](const auto &req, auto &res, std::exception_ptr ep) {
        res.status = httplib::OK_200;
        std::string msg;
        try {
            std::rethrow_exception(ep);
        } catch (const std::exception &e) {
            msg = e.what();
        } catch (...) {
            msg = "unknown exception";
        }
        spdlog::error("exception: {}", msg);
        res.set_content(BuildFailed(msg), "application/json");
    });

    // 设备信息接口（附带打印）
    server.Get("/usage/device_info", [&collector, bandwidth_fluctuate, disconnect_sec, reconnect_sec](const httplib::Request &, httplib::Response &res) {
        DeviceStatus dev_info;
        dev_info.disconnectTime = disconnect_sec;
        dev_info.reconnectTime = reconnect_sec;
        dev_info.timeWindow = 5;
        dev_info.cpu_used = collector.GetCpuUsage();
        dev_info.mem_used = collector.GetMemoryUsage();
        dev_info.xpu_used = collector.GetNpuUsage();
        dev_info.net_latency = collector.GetNetLatency(); // ms

        // 处理带宽波动
        double bandwidth;
        if (bandwidth_fluctuate) {
            bandwidth = bandwidth_dist(gen);
        } else {
            bandwidth = collector.GetNetBandwidth();
        }
        dev_info.net_bandwidth = bandwidth;

        // 采集日志改为 debug，避免高频刷屏（master 会周期性拉取）
        spdlog::debug(
            "device_info cpu={:.2f}% mem={:.2f}% xpu={:.2f}% latency_ms={} bandwidth_mbps={:.2f} disconnect={} reconnect={}",
            dev_info.cpu_used * 100,
            dev_info.mem_used * 100,
            dev_info.xpu_used * 100,
            dev_info.net_latency,
            dev_info.net_bandwidth,
            dev_info.disconnectTime,
            dev_info.reconnectTime
        );

        // 构建响应
        json payload = dev_info.to_json();
        payload["services"] = GetRunningBackendsSnapshot();
        std::string result = BuildSuccess(payload);
        res.set_content(result, "application/json");
    });

    // 仅供本机 recv_server 调用：确保某个 service 的后端已启动（按需启动）
    server.Post("/ensure_service", [](const httplib::Request &req, httplib::Response &res) {
        try {
            if (!g_allow_remote_control) {
                if (req.remote_addr != "127.0.0.1" && req.remote_addr != "::1" && req.remote_addr != "localhost") {
                    res.status = 403;
                    res.set_content(BuildFailed({{"error", "forbidden"}}), "application/json");
                    return;
                }
            }

            json j = json::parse(req.body);
            if (!j.contains("service") || !j["service"].is_string()) {
                res.status = 400;
                res.set_content(BuildFailed({{"error", "missing service"}}), "application/json");
                return;
            }
            std::string service = j["service"].get<std::string>();
            if (service.empty()) {
                res.status = 400;
                res.set_content(BuildFailed({{"error", "empty service"}}), "application/json");
                return;
            }

            bool ok = EnsureBackendStarted(service);
            if (!ok) {
                res.status = 500;
                res.set_content(BuildFailed({{"error", "ensure_service failed"}, {"service", service}}), "application/json");
                return;
            }
            res.status = 200;
            res.set_content(BuildSuccess({{"service", service}, {"running_services", GetRunningBackendsSnapshot()}}), "application/json");
        } catch (const std::exception &e) {
            res.status = 400;
            res.set_content(BuildFailed({{"error", e.what()}}), "application/json");
        }
    });

    server.Get("/usage/services", [](const httplib::Request &, httplib::Response &res) {
        res.status = 200;
        res.set_content(BuildSuccess({{"running_services", GetRunningBackendsSnapshot()}}), "application/json");
    });

    // 启动服务器
    spdlog::info("Starting docker scheduler agent on port {}", kAgentPort);
    if (!server.listen("0.0.0.0", kAgentPort)) {
        spdlog::error("Failed to start server");
        g_is_running = false; // 通知线程退出
        auto_connect_thread.join(); // 等待线程结束
        return 1;
    }

    // 服务器退出时，通知线程并等待结束
    g_is_running = false;
    StopAllManagedChildren();
    auto_connect_thread.join();

    return 0;
}
