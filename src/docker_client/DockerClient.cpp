//
// Created by lxsa1 on 6/9/2024.
//

#include "DockerClient.h"
#include<string>
#include<iostream>
#include <nlohmann/json.hpp>
using namespace std;
// 默认的createContainer的json
const nlohmann::json null_obj_json;
const vector<bool> null_bool_arr;



string DockerClient::cmd2apipath(string cmd) {
    return "/"+docker_version + cmd;
}

// void DealErr(httplib::Result res) {
//
// }

DockerClient::DockerClient(std::string host, int port, string docker_version): host(host), port(port),  docker_version(docker_version), client(httplib::Client(host, port)) {
}

DockerClient::DockerClient(std::string host, int port, string docker_version, int read_timeout_sec): host(host), port(port),  docker_version(docker_version), client(httplib::Client(host, port)) {
    client.set_read_timeout(read_timeout_sec, 0);
}

DockerClient::~DockerClient() {
}



string DockerClient::ListContainers() {
    string cmd = "/containers/json";
    string api_path = cmd2apipath(cmd);
    httplib::Result res = client.Get(api_path);
    // 这里有一个疑问，res可以为nullptr  但是res不是一个对象吗，为什么res->body可以出现空指针访问异常
    if (res && res->status == httplib::StatusCode::OK_200) {
        return res->body;
    }else {
        // 错误处理
        auto err = res.error();
        std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
    }
    return "";
}


// host_config_binds volume绑定路径
string DockerClient::CreateContainer(string container_name, string image, vector<string> cmds, vector<string> args,bool host_config_privileged, vector<string> env, vector<string> host_config_binds, vector<string> devices, string host_ip, int host_port, int container_port, bool has_tty, string network_config) {
    string cmd = "/containers/create";
    string api_path = cmd2apipath(cmd);

    // 设置query_param  之后会拼接在api_path后
    httplib::Params query_params;
    query_params.emplace("name", container_name);

    nlohmann::json req_json;
    req_json["Cmd"] = cmds;
    req_json["Env"] = env;
    req_json["Image"] = image;
    req_json["Args"] = args;
    req_json["Tty"] = has_tty;

    // 容器本身端口
    // "ExposedPorts": {
    //     "80/tcp": {}
    // },
    nlohmann::json exposed_ports_json;
    exposed_ports_json[to_string(container_port)+"/tcp"] = null_obj_json;
    req_json["ExposedPorts"] = exposed_ports_json;



    //其他配置
    req_json["AttachStdout"] = true;
    req_json["AttachStderr"] = true;


    // "HostConfig": {
    //     "Binds":["/tmp:/tmp"],
    //     "Devices": [
    //         {
    //             "PathOnHost": "/dev/svm0",
    //             "PathInContainer": "/dev/svm0",
    //             "CgroupPermissions": "rwm"
    //         },
    //         {
    //             "PathOnHost": "/dev/ts_aisle",
    //             "PathInContainer": "/dev/ts_aisle",
    //             "CgroupPermissions": "rwm"
    //         },
    //     ],
    //     "PortBindings": {
    //         "22/tcp":[{"HostIP":"0.0.0.0", "HostPort":"11022"}]
    //     }
    // }
    nlohmann::json host_config_json;
    host_config_json ["Binds"] = host_config_binds; // volume映射关系"Binds": ["/tmp:/tmp"]

     // PortBindings 容器端口和主机之间的映射关系

    vector<nlohmann::json> portbindings ;
    nlohmann::json host_config_portbind_json;
    host_config_portbind_json["HostIp"]= host_ip;
    host_config_portbind_json["HostPort"] = to_string(host_port);
    portbindings.push_back(host_config_portbind_json);
    nlohmann::json single_container_port_json;
    single_container_port_json[to_string(container_port)+"/tcp"] = portbindings; // single_container_port_json key是容器端口 绑定着一个数组，数组元素为本机ip:port

    host_config_json["PortBindings"] = single_container_port_json; // 设置单个portBinding 绑定容器端口和主机端口

    // devices 映射关系
    vector<nlohmann::json> device_jsons;
    for(auto device_path : devices) {
        nlohmann::json device_json;
        device_json["PathOnHost"] = device_path;
        device_json["PathInContainer"] = device_path;
        device_json["CgroupPermissions"] = "rwm";
        device_jsons.push_back(device_json);
    }
    host_config_json["Devices"] = device_jsons;

    // 容器主机特权
    host_config_json["Privileged"] = host_config_privileged;

    req_json["HostConfig"] = host_config_json;
    string req_body = req_json.dump();

    string final_url = httplib::append_query_params(api_path, query_params);
    httplib::Result res = client.Post(final_url, req_body, "application/json");

    if (res && res.error() == httplib::Error::Success) {
        nlohmann::json res_json = nlohmann::json::parse(res->body);
        // 注意这里http请求返回状态码的201
        switch (res->status) {
            case httplib::StatusCode::Created_201:
                return res_json["Id"];
                break;
            case httplib::StatusCode::BadRequest_400:
                cout << "CreateContainer bad parameter" << endl;
                break;
            case httplib::StatusCode::NotFound_404:
                cout << "CreateContainer not sunch image" << res->body << endl;
                break;
            case httplib::StatusCode::Conflict_409:
                cout << "[ERROR] create container failed  " << "[params container_name]:" <<  container_name << " image:"<< image << "[result]:" << res->body << endl;
                break;
            case httplib::StatusCode::InternalServerError_500:
                cout << "CreateContainer server errror" << "[result]:" << res->body << endl;
                break;
            default:
                cout << "CreateContainer unknow errror [result:" << res->body << "]" << endl;
                break;
        }
    } else {
        // 链接失败等处理
        auto err = res.error();
        std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
    }
    return "";
}

std::string DockerClient::CreateContainer(CreateContainerParam param) {
    return CreateContainer(param.container_name,
                        param.image,
                        param.cmds,
                        param.args,
                        param.host_config_privileged,
                        param.env,
                        param.host_config_binds,
                        param.devices,
                        param.host_ip,
                        param.host_port,
                        param.container_port,
                        param.has_tty,
                        param.network_config
                    );
}

bool DockerClient::StopContainer(std::string container_id) {
    string cmd = "/containers/" +  container_id + "/stop";
    string api_path = cmd2apipath(cmd);
    httplib::Result res = client.Post(api_path, "", "application/json");
    if (res && res.error() == httplib::Error::Success) {
        switch (res->status) {
            case httplib::StatusCode::NoContent_204: // no error
                return true;
            break;
            case httplib::StatusCode::NotModified_304:
                cout << "container already stopped" << endl;
            break;
            case httplib::StatusCode::NotFound_404:
                cout << "no such container" << res->body <<endl;
            break;
            case httplib::StatusCode::InternalServerError_500:
                cout << "server errror" << res->body << endl;
            break;
            default:
                cout << "unknow errror [result:" << res->body << "]" << endl;
                break;
        }
    }else {
        // 错误处理
        auto err = res.error();
        std::cout << "[ignored]stopcontaier api, server close connection before it stop container, HTTP error: " << httplib::to_string(err) << std::endl;
    }
    return false;
}

bool DockerClient::StartContainer(string container_id) {
    string cmd = "/containers/" +  container_id + "/start";
    string api_path = cmd2apipath(cmd);
    httplib::Result res = client.Post(api_path, "", "application/json");
    if (res && res.error() == httplib::Error::Success) {
        switch (res->status) {
            case httplib::StatusCode::NoContent_204: // no error
                return true;
                break;
            case httplib::StatusCode::NotModified_304:
                cout << "container already started" << endl;
                break;
            case httplib::StatusCode::NotFound_404:
                cout << "no such container" << endl;
                break;
            case httplib::StatusCode::InternalServerError_500:
                cout << "server errror" << res->body << endl;
                break;
            default:
                cout << "unknow errror [result:" << res->body << "]" << endl;
                break;
        }
    }else {
        // 错误处理
        auto err = res.error();
        std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
    }
    return false;
}

bool DockerClient::PauseContainer(string container_id) {
    string cmd = "/containers/" +  container_id + "/pause";
    string api_path = cmd2apipath(cmd);
    httplib::Result res = client.Post(api_path, "", "application/json");
    if (res && res.error() == httplib::Error::Success) {
        switch (res->status) {
            case httplib::StatusCode::NoContent_204: // no error
                return true;
            case httplib::StatusCode::NotFound_404:
                cout << "no such container" << endl;
                break;
            case httplib::StatusCode::InternalServerError_500:
                cout << "server errror" << res->body << endl;
                break;
            default:
                cout << "unknow errror [result:" << res->body << "]" << endl;
                break;
        }
    }else {
        // 错误处理
        auto err = res.error();
        std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
    }
    return false;
}

bool DockerClient::UnpauseContainer(string container_id) {
    string cmd = "/containers/" +  container_id + "/unpause";
    string api_path = cmd2apipath(cmd);
    httplib::Result res = client.Post(api_path, "", "application/json");
    if (res && res.error() == httplib::Error::Success) {
        switch (res->status) {
            case httplib::StatusCode::NoContent_204: // no error
                return true;
            case httplib::StatusCode::NotFound_404:
                cout << "no such container" << endl;
            break;
            case httplib::StatusCode::InternalServerError_500:
                cout << "server errror [result:" << res->body << "]" << endl;
            break;
            default:
                cout << "unknow errror [result:" << res->body << "]" << endl;
                break;
        }
    }else {
        // 错误处理
        auto err = res.error();
        std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
    }
    return false;
}

bool DockerClient::KillContainer(std::string container_id) {
    string cmd = "/containers/" +  container_id + "/kill";
    string api_path = cmd2apipath(cmd);
    httplib::Result res = client.Post(api_path, "", "application/json");
    if (res && res.error() == httplib::Error::Success) {
        switch (res->status) {
            case httplib::StatusCode::NoContent_204: // no error
                return true;
            case httplib::StatusCode::NotFound_404:
                cout << "no such container" << endl;
            break;
            case httplib::StatusCode::Conflict_409:
                cout << "container is not running" << endl;
            break;
            case httplib::StatusCode::InternalServerError_500:
                cout << "server errror [result:" << res->body << "]" << endl;
            break;
            default:
                cout << "unknow errror [result:" << res->body << "]" << endl;
                break;
        }
    }else {
        // 错误处理
        auto err = res.error();
        std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
    }
    return false;
}
// Docker API params
// -v    Default: false  Remove anonymous volumes associated with the container.
// -link Default: false  Remove the specified link associated with the container
bool DockerClient::RemoveContainer(std::string container_id,bool v, bool force, bool link){
    string cmd = "/containers/" +  container_id;
    string api_path = cmd2apipath(cmd);
    // 设置query_param
    httplib::Params query_params;
    httplib::append_query_params(api_path, query_params);
    query_params.emplace("v",  std::to_string(v));
    query_params.emplace("force", std::to_string(force));
    query_params.emplace("link", std::to_string(link));

    string final_url = httplib::append_query_params(api_path, query_params);


    httplib::Result res = client.Delete(final_url, "", "application/json");
    if (res && res.error() == httplib::Error::Success) {
        switch (res->status) {
            case httplib::StatusCode::NoContent_204: // no error
                return true;
            case httplib::StatusCode::BadRequest_400:
                cout << "bad parameter" << endl;
            case httplib::StatusCode::NotFound_404:
                cout << "no such container" << endl;
                break;
            case httplib::StatusCode::InternalServerError_500:
                cout << "server errror [result:" << res->body << "]" << endl;
                break;
            default:
                cout << "unknow errror [result:" << res->body << "]" << endl;
                break;
        }
    }else {
        // 错误处理
        auto err = res.error();
        std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
    }
    return false;
}


string DockerClient::ListImages() {
    string cmd = "/images/json";
    string api_path = cmd2apipath(cmd);

    httplib::Result res = client.Get(api_path);
    // 这里有一个疑问，res可以为nullptr  但是res不是一个对象吗，为什么res->body可以出现空指针访问异常
    if (res && res->status == httplib::StatusCode::OK_200) {
        return res -> body;
    }else {
        // 错误处理
        auto err = res.error();
        std::cout << "HTTP error: " << httplib::to_string(err) << std::endl;
    }
    return "";
}
