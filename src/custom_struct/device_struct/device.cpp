//
// Created by lxsa1 on 22/10/2024.
//
#include "device.h"
#include <algorithm>
#include <cctype>

TaskType StrToTaskType(const std::string& str) {
    std::string s = str;
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), s.end());

    // 前提：tasktype == service name，完全一致（大小写也一致）
    if (s == "YoloV5") return YoloV5;
    if (s == "MobileNet") return MobileNet;
    if (s == "Bert") return Bert;
    if (s == "ResNet50") return ResNet50;
    if (s == "deeplabv3") return deeplabv3;
    if (s == "transcoding") return transcoding;
    if (s == "decoding") return decoding;
    if (s == "encoding") return encoding;

    return Unknown; // 返回未知类型
}


std::string GetDockerVersion(const Device& dev) {
    std::string docker_version;

    if(dev.type==DeviceType::ATLAS_H){
        docker_version = "v1.47";
    }else if(dev.type==DeviceType::RK3588){
        docker_version = "v1.45";
    }else if(dev.type==DeviceType::ATLAS_L){
        docker_version = "v1.39";
    }else{
        docker_version = "v1.39";
    }
    return docker_version;
}


