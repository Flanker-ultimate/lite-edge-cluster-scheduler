//
// Created by lxsa1 on 6/9/2024.
//
#ifndef DOCKERCLIENT_H
#define DOCKERCLIENT_H

#include<string>
#include <httplib.h>
#include <ostream>

typedef struct CreateContainerParam {
    CreateContainerParam(const std::string &container_name, const std::string &image,
                         const std::vector<std::string> &cmds, const std::vector<std::string> &args, bool host_config_privileged,
                         const std::vector<std::string> &env, const std::vector<std::string> &host_config_binds,
                         const std::vector<std::string> &devices, const std::string &host_ip, int host_port, int container_port,
                         bool has_tty, const std::string &network_config)
        : container_name(container_name),
          image(image),
          cmds(cmds),
          args(args),
          host_config_privileged(host_config_privileged),
          env(env),
          host_config_binds(host_config_binds),
          devices(devices),
          host_ip(host_ip),
          host_port(host_port),
          container_port(container_port),
          has_tty(has_tty),
          network_config(network_config) {
    }

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

    // toString method to convert the structure to a string
    std::string toString() const {
        std::ostringstream oss;
        oss << "Container Name: " << container_name << "\n";
        oss << "Image: " << image << "\n";
        oss << "Commands: [";
        for (const auto &cmd : cmds) {
            oss << cmd << ", ";
        }
        if (!cmds.empty()) oss.seekp(-2, oss.cur);  // Remove the last ", "
        oss << "]\n";

        oss << "Arguments: [";
        for (const auto &arg : args) {
            oss << arg << ", ";
        }
        if (!args.empty()) oss.seekp(-2, oss.cur);  // Remove the last ", "
        oss << "]\n";

        oss << "Privileged: " << (host_config_privileged ? "true" : "false") << "\n";

        oss << "Environment Variables: [";
        for (const auto &e : env) {
            oss << e << ", ";
        }
        if (!env.empty()) oss.seekp(-2, oss.cur);  // Remove the last ", "
        oss << "]\n";

        oss << "Host Config Binds: [";
        for (const auto &bind : host_config_binds) {
            oss << bind << ", ";
        }
        if (!host_config_binds.empty()) oss.seekp(-2, oss.cur);  // Remove the last ", "
        oss << "]\n";

        oss << "Devices: [";
        for (const auto &device : devices) {
            oss << device << ", ";
        }
        if (!devices.empty()) oss.seekp(-2, oss.cur);  // Remove the last ", "
        oss << "]\n";

        oss << "Host IP: " << host_ip << "\n";
        oss << "Host Port: " << host_port << "\n";
        oss << "Container Port: " << container_port << "\n";
        oss << "TTY Enabled: " << (has_tty ? "true" : "false") << "\n";
        oss << "Network Config: " << network_config << "\n";

        return oss.str();
    }
}CreateContainerParam;

class DockerClient {
private:
    httplib::Client client; // 默认port初始化为80端口
    std::string host;
    int port;
    std::string docker_version;
private:
    // for example input cmd = /images/json   return  /v1.44/images/json
    std::string cmd2apipath(std::string cmd);

public:
    DockerClient();

    DockerClient(std::string host, int port, std::string docker_version);

    DockerClient(std::string host, int port, std::string docker_version, int read_timeout_sec);

    ~DockerClient();

    // container
    std::string ListContainers();
    ///

    /// @param host_config 内部会有devices指定
    /// @param network_config 网络端口的配置
    /// @return

    ///
    /// @param container_name 容器名
    /// @param cmds 容器执行的命令
    /// @param image 镜像名称
    /// @param env 容器内的环境变量 "Env": ["FOO=bar" , "BAZ=quux"]
    /// @param host_config_binds volume 绑定关系 ["/tmp:tmp", "/var:/var"]
    /// @param devices 主机提供给容器的设备路径 ["/dev/dvpp_cmdlist", "/dev/pngd"]
    /// @param host_port 主机端口
    /// @param container_port 容器端口   这里默认会进行host_port的绑定
    /// @param network_config
    /// @return 创建后的容器id    空字符串表示错误
    std::string  CreateContainer(std::string container_name ,std::string image, std::vector<std::string> cmds, std::vector<std::string> args, bool host_config_privileged, std::vector<std::string> env, std::vector<std::string> host_config_binds, std::vector<std::string> devices, std::string host_ip, int host_port, int container_port, bool has_tty, std::string network_config);

    std::string CreateContainer(CreateContainerParam param);



    ///
    /// @param container_id 容器id
    /// @return 注意：这里总会返回false 因为发生 failed read on connection 原因是server端关闭容器需要会有一段时间，默认读取时间是5s,这时 client这边强制关闭链接 发生读取body错误
    bool StopContainer(std::string container_id);

    ///
    /// @param container_idorname container id or name
    /// @return
    bool StartContainer(std::string container_id);

    bool PauseContainer(std::string container_id);

    bool UnpauseContainer(std::string container_id);

    bool KillContainer(std::string container_id);

    ///
    /// @param container_id
    /// @param v   default=false  Remove anonymous volumes associated with the container.
    /// @param force If the container is running, kill it before removing it.
    /// @param link Remove the specified link associated with the container
    /// @return
    bool RemoveContainer(std::string container_id,bool v, bool force, bool link);

    // images
    std::string ListImages();
};


#endif //DOCKERCLIENT_H
