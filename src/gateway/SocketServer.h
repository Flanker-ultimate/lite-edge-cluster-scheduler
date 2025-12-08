//
// Created by lxsa1 on 25/11/2024.
//

#ifndef SOCKETSERVER_H
#define SOCKETSERVER_H
#include "string"
#include "spdlog/spdlog.h"


class SocketServer {
public:
    SocketServer(const std::string &ip, int port)
        : ip(ip),
          port(port) {
    }

    // handle quest to transfer tgt host
    void HandleQuest();
    // start socket server to listen client http quest
    int Start();
private:
    std::string ip;
    int port;
};



#endif //SOCKETSERVER_H
