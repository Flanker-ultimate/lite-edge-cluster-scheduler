#include <iostream>
#include<scheduler.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <vector>
#include <mutex>
nlohmann::json json_atlas_h = {
        {"type",       "ATLAS_H"},
        {"global_id",  "123e4567-e89b-12d3-a456-426614174000"},
        {"ip_address", "192.168.58.4"},
        {"agent_port", 8000}
};
nlohmann::json json_atlas_l = {
        {"type",       "ATLAS_L"},
        {"global_id",  "123e4567-e89b-12d3-a456-426614174111"},
        {"ip_address", "192.168.58.5"},
        {"agent_port", 8000}
};
nlohmann::json json_rk3588 = {
        {"type",       "rk3588"},
        {"global_id",  "123e4567-e89b-12d3-a456-426614174222"},
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
nlohmann::json yolo4Atlas = {
    {"mem", 0.1},
    {"cpu_used", 0.08},
    {"xpu_used", 0.1},
    {"net_latency", 0},
    {"net_bandwidth",0}
};
nlohmann::json yolo4Atlas_L = {
    {"mem", 0.15},
    {"cpu_used", 0.1},
    {"xpu_used", 0.2},
    {"net_latency", 0},
    {"net_bandwidth",0}
};
std::mutex mtx;  // 用于保护共享资源
void thread_task(int thread_id, int iterations,Docker_scheduler scheduler) {
    for (int i = 0; i < iterations; ++i) {
        std::lock_guard<std::mutex> lock(mtx);
        std::cout << "Thread " << thread_id << " Running Z3 mutiway scheduling test iteration: " << (i + 1) << std::endl;
        Device target = Docker_scheduler::Z3_schedule_v2(YoloV5);

        if (target.type == ATLAS_H)
            std::cout << "Select ATLAS_H" << std::endl;
        else if (target.type == ATLAS_L)
            std::cout << "Select ATLAS_L" << std::endl;
        else
            std::cout << "Select RK3588" << std::endl;

        DeviceStatus newstatus;
        if(target.type==ATLAS_H){
            newstatus.from_json(yolo4Atlas);
        }
        else{
            newstatus.from_json(yolo43588);
        }
        // 仅为更新状态加锁
        scheduler.updateStatus(target.global_id, newstatus);
        scheduler.display_devstatus(target.global_id);
    }
}
// test  parseJson and RegisNode
TEST(DeviceTest, ParseAndRegisterNodes) {
    Device device;

     // parse and register ATLAS_H
     EXPECT_NO_THROW(device.parseJson(json_atlas_h));
     EXPECT_NO_THROW(Docker_scheduler::RegisNode(device));
     std::cout << "Atlas-H Node registered successfully\n";

    // parse and register ATLAS-L
    EXPECT_NO_THROW(device.parseJson(json_atlas_l));
    EXPECT_NO_THROW(Docker_scheduler::RegisNode(device));
    std::cout << "Atlas-L Node registered successfully\n";

     // parse and register rk3588
     EXPECT_NO_THROW(device.parseJson(json_rk3588));
     EXPECT_NO_THROW(Docker_scheduler::RegisNode(device));
     std::cout << "RK3588 Node registered successfully\n";
     //getchar();
     Docker_scheduler::startDeviceInfoCollection();

    // parse and register ATLAS_H
    EXPECT_NO_THROW(device.parseJson(json_atlas_h));
    EXPECT_NO_THROW(Docker_scheduler::Disconnect_device(device));
    std::cout << "Atlas-H Node removed successfully\n";
}

TEST(DeviceTest, Disconnect) {
    Device device;



}
//
//// test Docker_scheduler::Z3_schedule
//TEST(SchedulerTest, ScheduleDevice) {
//    std::string test_file = "../../../tests/z3_mock_test/mock_data.json";
//
//    // create schduler
//    //EXPECT_NO_THROW(Docker_scheduler scheduler(test_file));
//    Docker_scheduler scheduler(test_file);
//        int counter_atlas_h=0;
//        int counter_atlas_l=0;
//        int counter_rk3588=0;
//    // loop Z3_schedule 10 times
//    for (int i = 0; i < 1080; ++i) {
//        std::cout << "Running Z3 scheduling test iteration: " << (i + 1) << std::endl;
//        // if(i==10){
//        //     Device device;
//        //     // parse and register rk3588
//        //     EXPECT_NO_THROW(device.parseJson(json_atlas_h));
//        //     EXPECT_NO_THROW(Docker_scheduler::RegisNode(device));
//        //     std::cout << "ATLAS_H Node registered successfully\n";
//        // }
//        // if(i==30){
//        //     Device device;
//        //     // parse and register rk3588
//        //     EXPECT_NO_THROW(device.parseJson(json_rk3588));
//        //     EXPECT_NO_THROW(Docker_scheduler::RegisNode(device));
//        //     std::cout << "RK3588 Node registered successfully\n";
//        // }
//        // call Z3_schedule to get target device
//        //EXPECT_NO_THROW({
//                            Device target = Docker_scheduler::Z3_schedule_v2(YoloV5);
//                            DeviceStatus newstatus;
//                            if(target.type==ATLAS_H){
//                                counter_atlas_h++;
//                                cout<<"Select ATLAS_H"<<endl;
//                                //每增加50个任务，ATLAS_H的负载就增加一倍
//                                if(counter_atlas_h%50==0||counter_atlas_h==0){
//                                    newstatus.from_json(yolo4Atlas);
//                                    scheduler.updateStatus(target.global_id,newstatus);
//                                    scheduler.regissrv(target.global_id,YoloV5);
//                                }
//                            }
//                            else if(target.type==ATLAS_L){
//                                counter_atlas_l++;
//                                cout<<"Select ATLAS_L"<<endl;
//                                if(counter_atlas_l%25==0||counter_atlas_l==0){
//                                    newstatus.from_json(yolo4Atlas_L);
//                                    scheduler.updateStatus(target.global_id,newstatus);
//                                    scheduler.regissrv(target.global_id,YoloV5);
//                                }
//                            }
//                            else{
//                                counter_rk3588++;
//                                cout<<"Select RK3588"<<endl;
//                                if(counter_rk3588%40==0||counter_rk3588==0){
//                                    newstatus.from_json(yolo43588);
//                                    scheduler.updateStatus(target.global_id,newstatus);
//                                    scheduler.regissrv(target.global_id,YoloV5);
//                                }
//                            }
//
//                            //scheduler.display_devstatus(target.global_id);
//                        //});
//        //getchar();
//    }
//    cout<<"ATLAS_H: "<<counter_atlas_h<<endl;
//    cout<<"ATLAS_L: "<<counter_atlas_l<<endl;
//    cout<<"RK3588: "<<counter_rk3588<<endl;
//    scheduler.display_dev();
//}
/*
TEST(Scheduler_mutiway_Test, ScheduleDevice) {
    std::string test_file = "../../../tests/z3_mock_test/mock_data.json";

    // create schduler
    //EXPECT_NO_THROW(Docker_scheduler scheduler(test_file));
    const int num_threads = 20;  // 创建 20 个线程
    const int iterations_per_thread = 1;  // 每个线程执行 1 次

    std::vector<std::thread> threads;

    // 创建并启动线程
    for (int i = 0; i < num_threads; ++i) {
        Docker_scheduler scheduler(test_file);  // 每个线程创建一个新的 scheduler 实例
        threads.push_back(thread(thread_task, i + 1, iterations_per_thread,scheduler));
    }

    // 等待所有线程完成
    for (auto& t : threads) {
        t.join();
    }
    std::cout << "所有任务执行完毕" << std::endl;
}*/
/*// test Docker_scheduler::Z3_simulate_schedule
TEST(SchedulerTest, Schedule_simulate_Device) {
    // loop Z3_schedule 10 times
    for (int i = 0; i < 10; ++i) {
        std::cout << "Running Z3 simulate scheduling test iteration: " << (i + 1) << std::endl;

        // call Z3_simulate_schedule to get target device,check avg_transfer funtion
        EXPECT_NO_THROW({
                            Device target = Docker_scheduler::Z3_simulate_schedule(YoloV5, 0.5,0,0.5);//ATLAS-H;ATLAS-L;RK3588
                            target.show();
                        });
    }

    // loop Z3_simulate_schedule 10 times
    for (int i = 0; i < 10; ++i) {
        std::cout << "Running Z3 simulate scheduling test iteration: " << (i + 1) << std::endl;

        // call Z3_simulate_schedule to get target device,check lowest lantency funtion
        EXPECT_NO_THROW({

                            Device target = Docker_scheduler::Z3_simulate_schedule(YoloV5,1,0,0);//ATLAS-H;ATLAS-L;RK3588
                            target.show();
                        });

    }

}
*/



//int main(int argc, char **argv) {
//    ::testing::InitGoogleTest(&argc, argv);
//    return RUN_ALL_TESTS();
//}