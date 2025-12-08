#include "SocketServer.h"
#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <fcntl.h>
#include <scheduler.h>
#include <TimeRecorder.h>

#define BUFFER_SIZE 4096
#define MAX_EVENTS 10
struct ERROR {
    int errorCode;
    const char* errorMsg;
    ERROR(int errorCode, std::string errorMsg) {
        this->errorCode= errorCode;
        this->errorMsg = errorMsg.c_str();
    };
};
ERROR SERVE_NOAVAILABLE_ERROR(511, "we can't get a useful srv");
ERROR CONNECTION_TARGET__ERROR(512, "Connect target_server failed");

void sendErrorResponse(int client_sock, ERROR& error) {
    // HTTP 响应头部
    std::string http_response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "Error Code: " + std::to_string(error.errorCode) + "\r\n"  // 错误码
        "Error Message: " + error.errorMsg + "\r\n";                // 错误消息

    // 发送响应
    ssize_t bytes_sent = send(client_sock, http_response.c_str(), http_response.length(), 0);
    if (bytes_sent == -1) {
        spdlog::error("Send failed!");
    } else {
        spdlog::info("can't deal request,Sent size:{}, content:{} !",bytes_sent, http_response);
    }
}


// Helper function to set a socket to non-blocking mode
int set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }
    flags |= O_NONBLOCK;
    if (fcntl(sockfd, F_SETFL, flags) == -1) {
        perror("fcntl F_SETFL");
        return -1;
    }
    return 0;
}

void handle_epoll_event(int epoll_fd, struct epoll_event *events, int num_events, int client_sock, int tgt_sock, bool &run_flag) {
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_sent;

    for (int i = 0; i < num_events; i++) {
        int fd = events[i].data.fd;
        // client_sock <- data; recv http reqt and transfer
        if (fd == client_sock && (events[i].events & EPOLLIN)) {
            // Read from client and forward to target server
            bytes_read = recv(client_sock, buffer, sizeof(buffer), 0);
            if (bytes_read > 0) {
                // Forward data to target server
                // notice send return error code such like  other side buff is full
                bytes_sent = send(tgt_sock, buffer, bytes_read, 0);
                // TODO(linxuesong): cannot read buffer as string
                // spdlog::debug("tranfer to target  byte_read_size:{}, bytes_sent:{}, data:{})", bytes_read, bytes_sent, buffer);
                if (bytes_sent == -1) {
                    spdlog::error("Error sending data to target server");
                    run_flag = false; // consider target server close
                    break;
                }
            } else if (bytes_read == 0) {
                // Client closed the connection means quest completed
                spdlog::debug( "client disconnected");
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_sock, nullptr);
                run_flag = false;
                break;
            } else {
                // bytes_read = -1 represent error
                spdlog::error("Error reading from client socket");
                run_flag = false;
                break;
            }
        }
        // TODO(linxuesong): split duplicate code into function
        // tgt_sock <- data;    recv http resp and transfer
        if (fd == tgt_sock && (events[i].events & EPOLLIN)) {
            // Read from target server and forward to client
            bytes_read = recv(tgt_sock, buffer, sizeof(buffer), 0);
            if (bytes_read > 0) {
                // Forward data to client
                bytes_sent = send(client_sock, buffer, bytes_read, 0);
                // TODO(linxuesong): cannot read buffer as string
                // spdlog::info("tranfer to client data:{}", buffer);
                 if (bytes_sent == -1) {
                    run_flag = false;
                    spdlog::error("Error sending data to client");
                    break;
                }
            } else if (bytes_read == 0) {
                // Target server closed the connection
                spdlog::debug("target server disconnected" );
                epoll_ctl(epoll_fd, EPOLL_CTL_DEL, tgt_sock, nullptr);
                run_flag = false;
                break;
            } else {
                spdlog::error("Error reading from target server socket");
                run_flag = false;
                break;
            }
        }
    }
}


// client_sock connected with origin host
// tgt_sock connecting with target host
// only read http head first line
// multi thread transfer other content
// return
//   1 success
//  -1 error
int handle_client(int client_sock) { // Step 1: Read the first line of the HTTP request
    TimeRecord<chrono::milliseconds> p2p_time_record("p2p_time_record"); // p2p time
    p2p_time_record.startRecord();
    char buffer[BUFFER_SIZE];
    std::string first_line;
    ssize_t bytes_read;

    // step 1:Read until we encounter the end of the first line
    while ((bytes_read = recv(client_sock, buffer, 1, 0)) > 0) {
        if (buffer[0] == '\n' && !first_line.empty() && first_line.back() == '\r') {
            break;
        }
        first_line += buffer[0];
    }

    spdlog::debug("recv firstline from client data:{}", first_line);
    // pares url params
    std::string taskid = "";
    std::string real_url = "";
    size_t question_mark_pos = first_line.find('?');
    if (question_mark_pos != std::string::npos) {
        // 提取查询字符串部分
        std::string query_string = first_line.substr(question_mark_pos + 1);
        // 查找 taskid 和 real_url 参数
        size_t taskid_pos = query_string.find("taskid=");
        if (taskid_pos != std::string::npos) {
            taskid = query_string.substr(taskid_pos + 7, query_string.find('&', taskid_pos) - (taskid_pos + 7));
        }else {
            spdlog::error("parse params failed, firstLine:{}", first_line);
            close(client_sock);
            return -1;
        }
        size_t real_url_pos = query_string.find("real_url=");
        if (real_url_pos != std::string::npos) {
            real_url = query_string.substr(real_url_pos + 9);
            size_t amp_pos = real_url.find(' ');
            if (amp_pos != std::string::npos) {
                real_url = real_url.substr(0, amp_pos);
            }
        }else {
            spdlog::error("parse params failed, firstLine:{}", first_line);
            close(client_sock);
            return -1;
        }
    }else {
        spdlog::error("parse params failed, firstLine:{}", first_line);
        close(client_sock);
        return -1;
    }
    TaskType task_type = StrToTaskType(taskid);
    // Change path to new target
    std::string modified_first_line = "POST /" + real_url + " HTTP/1.1\r\n";

    // Step 2: Set up connection to target server
    // get target_device_id
    TimeRecord<chrono::milliseconds> z3_time_record("z3_time_record");
    z3_time_record.startRecord();
    optional<SrvInfo> srv_info_opt = Docker_scheduler::getOrCrtSrvByTType(task_type);
    z3_time_record.endRecord();
    spdlog::info("Docker_scheduler::getOrCrtSrvByTType cost_time:{}", z3_time_record.getDuration());
    if (srv_info_opt == nullopt) {
        spdlog::error("we can't get a useful srv,Original first line: {}",first_line);
        // sendErrorResponse(client_sock, SERVE_NOAVAILABLE_ERROR);
        close(client_sock);
        return -1;
    }
    SrvInfo srv_info = srv_info_opt.value();
    std::string tgt_ip = srv_info.ip;
    int tgt_port = srv_info.port;

    int target_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (target_sock < 0) {
        spdlog::error("Socket creation failed");
        close(client_sock);
        return -1;
    }

    sockaddr_in target_addr{};
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(tgt_port);
    if (inet_pton(AF_INET, tgt_ip.c_str(), &target_addr.sin_addr) <= 0) {
        spdlog::error("Invalid address/ Address not supported");
        close(client_sock);
        close(target_sock);
        return -1;
    }

    if (connect(target_sock, (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
        spdlog::error("Connection to target failed");
        close(client_sock);
        close(target_sock);
        return -1;
    }

    // Step 3: Send the modified first line to the target server
    int first_line_send_size = send(target_sock, modified_first_line.c_str(), modified_first_line.size(), 0);
    if(first_line_send_size <= 0) {
        spdlog::error("Send first line of Http Request failed");
        close(client_sock);
        close(target_sock);
        return -1;
    }
    spdlog::debug("tranfer to target first line:{} ------------ tranfer to target first_line end---------------", modified_first_line);

    // Step 4: Set target_sock to non-blocking
    // if (set_nonblocking(client_sock) < 0) {
    //     std::cerr << "Error setting non-blocking mode for client_sock!" << std::endl;
    //     return -1;
    // }
    // if (set_nonblocking(target_sock) < 0) {
    //     std::cerr << "Error setting non-blocking mode for target_sock!" << std::endl;
    //     return -1;
    // }

    // Step 5: Create an epoll instance  tranfer other content of http quest and total http response
    int epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        spdlog::error("Error creating epoll instance");
        return -1;
    }

    // Add client_sock and tgt_sock to epoll
    struct epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = client_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_sock, &event) == -1) {
        spdlog::error("Error adding client_sock to epoll");
        return -1;
    }

    event.data.fd = target_sock;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, target_sock, &event) == -1) {
        spdlog::error("Error adding target_sock to epoll");
        return -1;
    }

    struct epoll_event events[MAX_EVENTS];

    // Step 7: Event loop until close connection
    TimeRecord<chrono::milliseconds> socket_time_record("socket_time_record");
    socket_time_record.startRecord();
    bool run_flag = true;
    while (run_flag) {
        int num_events = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (num_events == -1) {
            perror("Error waiting for epoll events");
            break;
        }
        handle_epoll_event(epoll_fd, events, num_events, client_sock, target_sock, run_flag);
    }
    socket_time_record.endRecord();
    spdlog::info("socket transfer http quest except firstline and response cost_time:{}", socket_time_record.getDuration());
    // Step 8: Close connections
    p2p_time_record.endRecord();

    spdlog::info("p2p cost_time:{}", p2p_time_record.getDuration());
    close(client_sock);
    close(target_sock);
    close(epoll_fd);

    return 0;
}

int SocketServer::Start() {
    // 1. create socket
    int server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd < 0) {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }

    // 2. set server address
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(this->port);
    server_addr.sin_addr.s_addr = INADDR_ANY;// set host ip

    // 3. bind socket
    if (bind(server_sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        spdlog::error("Failed to bind server socket");
        close(server_sockfd);
        return 1;
    }

    // 4. listen
    if (listen(server_sockfd, 5) < 0) {
        std::cerr << "Failed to listen on server socket\n";
        close(server_sockfd);
        return 1;
    }

    std::cout << "SocketServer started, listening on port " << this->port << std::endl;

    // 5. Accept client connect
    while (true) {
        int client_sockfd = accept(server_sockfd, nullptr, nullptr);
        if (client_sockfd < 0) {
            std::cerr << "Failed to accept client connection\n";
            continue;
        }

        // start multi thread to hanlde quest
        std::thread(handle_client, client_sockfd).detach();
    }

    // 6. close server socket
    close(server_sockfd);
    return 0;
}
