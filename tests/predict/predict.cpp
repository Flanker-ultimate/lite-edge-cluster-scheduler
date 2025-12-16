#include <spdlog/spdlog.h>
#include <vector>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include<scheduler.h>
nlohmann::json json_atlas_h = {
        {"type",       "ATLAS_H"},
        {"global_id",  "123e4567-e89b-12d3-a456-426614174584"},
        {"ip_address", "192.168.58.4"},
        {"agent_port", 8000}
};
nlohmann::json json_atlas_l = {
        {"type",       "ATLAS_L"},
        {"global_id",  "123e4567-e89b-12d3-a456-426614174585"},
        {"ip_address", "192.168.58.5"},
        {"agent_port", 8000}
};
nlohmann::json json_rk3588 = {
        {"type",       "rk3588"},
        {"global_id",  "123e4567-e89b-12d3-a456-426614174583"},
        {"ip_address", "192.168.58.3"},
        {"agent_port", 8000}
};

nlohmann::json yolo43588 = {
        {"mem", 0.08},
        {"cpu_used", 0.05},
        {"xpu_used", 0.07},
        {"net_latency", 0},
        {"net_bandwidth",0}
};
nlohmann::json yolo4Atlas_h = {
        {"mem", 0.1},
        {"cpu_used", 0.08},
        {"xpu_used", 0.03},
        {"net_latency", 0},
        {"net_bandwidth",0}
};
nlohmann::json yolo4Atlas_l = {
        {"mem", 0.15},
        {"cpu_used", 0.05},
        {"xpu_used", 0.06},
        {"net_latency", 0},
        {"net_bandwidth",0}
};

// test  parseJson and RegisNode
TEST(DeviceTest, ParseAndRegisterNodes) {
    Device device;
    // parse and register ATLAS_H
    EXPECT_NO_THROW(device.parseJson(json_atlas_h));
    EXPECT_NO_THROW(Docker_scheduler::RegisNode(device));
    spdlog::info("Atlas-H Node registered successfully");

    // parse and register ATLAS-L
    EXPECT_NO_THROW(device.parseJson(json_atlas_l));
    EXPECT_NO_THROW(Docker_scheduler::RegisNode(device));
    spdlog::info("Atlas-L Node registered successfully");

     //parse and register rk3588
    EXPECT_NO_THROW(device.parseJson(json_rk3588));
    EXPECT_NO_THROW(Docker_scheduler::RegisNode(device));
    spdlog::info("RK3588 Node registered successfully");
    //getchar();
}

TEST(StatusTest, Status_Update) {
    std::string test_file = "../../config_files/static_info.json";
    Docker_scheduler scheduler(test_file);
    int counter_atlas_h=0;
    int counter_atlas_l=0;
    int counter_rk3588=0;
    for (int i = 0; i < 1000; ++i) {
        spdlog::info("Running Predict test iteration: {}", i + 1);
        Device target = Docker_scheduler::Model_predict(YoloV5);
        scheduler.display_devstatus(target.global_id);
        DeviceStatus newstatus;
        if(target.type==ATLAS_H){
            counter_atlas_h++;
            //cout<<"select ATLAS-H"<<endl;
            //每增加10个任务，ATLAS_H的负载就增加一倍
            if(counter_atlas_h%10==0||counter_atlas_h==0){
                newstatus.from_json(yolo4Atlas_h);
                scheduler.updateStatus(target.global_id,newstatus);
                scheduler.regissrv(target.global_id,YoloV5);
            }
        }
        else if(target.type==ATLAS_L){
            counter_atlas_l++;
            //cout<<"select ATLAS-L"<<endl;
            if(counter_atlas_l%15==0||counter_atlas_l==0){
                newstatus.from_json(yolo4Atlas_l);
                scheduler.updateStatus(target.global_id,newstatus);
                scheduler.regissrv(target.global_id,YoloV5);
            }
        }
        else{
            counter_rk3588++;
            //cout<<"select RK3588"<<endl;
            if(counter_rk3588%20==0||counter_rk3588==0){
                newstatus.from_json(yolo43588);
                scheduler.updateStatus(target.global_id,newstatus);
                scheduler.regissrv(target.global_id,YoloV5);
            }
        }

    }
    spdlog::info("ATLAS_H: {}", counter_atlas_h);
    spdlog::info("ATLAS_L: {}", counter_atlas_l);
    spdlog::info("RK3588: {}", counter_rk3588);
}
