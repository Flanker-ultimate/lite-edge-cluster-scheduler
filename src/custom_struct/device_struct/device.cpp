//
// Created by lxsa1 on 22/10/2024.
//
#include "device.h"

TaskType StrToTaskType(const std::string& str) {
    if (str == "YoloV5") return YoloV5;
    else if (str == "MobileNet") return MobileNet;
    else if (str == "Bert") return Bert;
    else if (str == "ResNet50") return ResNet50;
    else if (str == "deeplabv3") return deeplabv3;
    else if (str == "transcoding") return transcoding;
    else if (str == "decoding") return decoding;
    else if (str == "encoding") return encoding;
    else return Unknown; // 返回未知类型
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


