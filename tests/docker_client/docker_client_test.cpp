//
// Created by lxsa1 on 12/9/2024.

//相关接口耗时汇总 单位ms
// Create               165、146
// Stop                 10042
// Start                840(首次) 436(二次)
// Pause                28
// Unpause              28
// Kill                 309  362 339
// Remove               63 65(force=false) 429(force=true 删除正在运行容器)
#include <gtest/gtest.h>
#include "TimeRecorder.h"
#include "DockerClient.h"
using namespace std;

typedef struct DockerClientTestParam {
    string host;
    int port;
    string docker_version;
    int read_timeout_sec;
    CreateContainerParam create_container_param;
}DockerClientTestParam;

class DockerClientTest : public testing::TestWithParam<DockerClientTestParam> {
public:
    // 保存全局的容器 ID
    std::string global_container_id;
protected:
    // TestSuiteName, TestName
    void TestCreateContainer() {
        DockerClientTestParam param = GetParam();
        DockerClient dc = DockerClient(param.host, param.port, param.docker_version, param.read_timeout_sec);
        TimeRecord<chrono::milliseconds> StartTime("CreateContainer");
        StartTime.startRecord();
        std::string created_container_id = dc.CreateContainer(param.create_container_param);
        if(created_container_id == "") {
            std::cout << "create container false" << std::endl;
        }else {
            global_container_id=created_container_id;
            std::cout << "create_container success: [created_container_id=" << created_container_id << "]" << std::endl;
        }
        StartTime.endRecord();
        StartTime.print();
        // created_container_id 空 则打印failed
        EXPECT_FALSE(created_container_id.empty()) << "createcontainer failed";
    }

    void TestStartContainer() {
        DockerClientTestParam param = GetParam();
        DockerClient dc = DockerClient(param.host, param.port, param.docker_version, param.read_timeout_sec);
        std::string container_id = global_container_id;

        TimeRecord<chrono::milliseconds> StartTime("StartContainer");
        StartTime.startRecord();
        bool res = dc.StartContainer(container_id);
        std::cout << "StartContainer invoke: [res=" << res << "]" << std::endl;
        StartTime.endRecord();
        StartTime.print();
        ASSERT_TRUE(res) << "StartContainer failed" << endl;
    }

    void TestPauseContainer() {
        DockerClientTestParam param = GetParam();
        DockerClient dc = DockerClient(param.host, param.port, param.docker_version, param.read_timeout_sec);
        std::string container_id = global_container_id;

        TimeRecord<chrono::milliseconds> StartTime("PauseContainer");
        StartTime.startRecord();
        bool start_res = dc.PauseContainer(container_id);
        std::cout << "PauseContainer invoke: [res=" << start_res << "]" << std::endl;
        StartTime.endRecord();
        StartTime.print();
    }

    void TestUnpauseContainer()  {
        DockerClientTestParam param = GetParam();
        DockerClient dc = DockerClient(param.host, param.port, param.docker_version, param.read_timeout_sec);
        std::string container_id = global_container_id;

        TimeRecord<chrono::milliseconds> StartTime("UnpauseContainer");
        StartTime.startRecord();
        bool start_res = dc.UnpauseContainer(container_id);
        std::cout << "UnpauseContainer invoke: [res=" << start_res << "]" << std::endl;
        StartTime.endRecord();
        StartTime.print();
    }

    // 我们不使用stop接口 会导致下面的kill接口报错  contianer is not running
    void TestStopContainer() {
        DockerClientTestParam param = GetParam();
        DockerClient dc = DockerClient(param.host, param.port, param.docker_version, param.read_timeout_sec);
        std::string container_id = global_container_id;

        TimeRecord<chrono::milliseconds> StartTime("StopContainer");
        StartTime.startRecord();
        bool res = dc.StopContainer(container_id);
        std::cout << "StopContainer invoke: [res=" << res << "]" << std::endl;
        StartTime.endRecord();
        StartTime.print();
        ASSERT_TRUE(res) << "StopContainer failed" << endl;
    }

    void TestKillContainer() {
        DockerClientTestParam param = GetParam();
        DockerClient dc = DockerClient(param.host, param.port, param.docker_version, param.read_timeout_sec);

        std::string container_id = global_container_id;

        TimeRecord<chrono::milliseconds> StartTime("KillContainer");
        StartTime.startRecord();
        bool res = dc.KillContainer(container_id);
        std::cout << "killContainer invoke: [res=" << res << "]" << std::endl;
        StartTime.endRecord();
        StartTime.print();
        ASSERT_TRUE(res) << "killContainer failed" << endl;
    }

    void TestRemoveContainer() {
        DockerClientTestParam param = GetParam();
        DockerClient dc = DockerClient(param.host, param.port, param.docker_version, param.read_timeout_sec);

        std::string container_id = global_container_id;

        TimeRecord<chrono::milliseconds> StartTime("RemoveContainer");
        StartTime.startRecord();
        bool res = dc.RemoveContainer(container_id, false, false, false);
        std::cout << "RemoveContainer invoke: [res=" << res << "]" << std::endl;
        StartTime.endRecord();
        StartTime.print();
        ASSERT_TRUE(res) << "assert_true" << endl;
    }

};




// 保存全局的容器 ID
std::string global_container_id;


const string host = "192.168.137.37";
const int port = 2375;
const string docker_version = "v1.47";
const int read_timeout_sec = 20;

const std::vector<std::string>  yolov_host_config_envs= {
   "CEND_VISIBLE_DEVICES=0",
   "ASCEND_ALLOW_LINK=True"
};

const std::vector<std::string>  yolov_host_config_devices= {
    "/dev/svm0"        ,
    "/dev/ts_aisle"    ,
    "/dev/upgrade"     ,
    "/dev/sys"         ,
    "/dev/vdec"        ,
    "/dev/vpc"         ,
    "/dev/pngd"        ,
    "/dev/venc"        ,
    "/dev/dvpp_cmdlist",
    "/dev/log_drv"     ,
 };


const std::vector<std::string>  yolov_host_config_binds= {
        "/etc/hdcBasic.cfg:/etc/hdcBasic.cfg",
    "/usr/lib64/libadump.so:/usr/lib64/libadump.so",
    "/usr/lib64/libstackcore.so:/usr/lib64/libstackcore.so",
    "/var/log/ascend_seclog:/var/log/ascend_seclog",
    "/usr/lib64/libdrvdsmi.so:/usr/lib64/libdrvdsmi.so",
    "/usr/local/sbin/npu-smi:/usr/local/sbin/npu-smi",
    "/usr/lib64/aicpu_kernels/:/usr/lib64/aicpu_kernels/",
    "/usr/lib64/libaicpu_processer.so:/usr/lib64/libaicpu_processer.so",
    "/etc/ascend_install.info:/etc/ascend_install.info",
    "/usr/lib64/libdevmmap.so:/usr/lib64/libdevmmap.so",
    "/usr/local/Ascend/driver/lib64:/usr/local/Ascend/driver/lib64",
    "/var/slogd:/var/slogd",
    "/var/davinci/driver:/var/davinci/driver",
    "/usr/lib64/libc_sec.so:/usr/lib64/libc_sec.so",
    "/usr/lib64/libmmpa.so:/usr/lib64/libmmpa.so",
    "/usr/lib64/libaicpu_prof.so:/usr/lib64/libaicpu_prof.so",
    "/usr/lib64/libaicpu_scheduler.so:/usr/lib64/libaicpu_scheduler.so",
    "/usr/lib64/libtsd_eventclient.so:/usr/lib64/libtsd_eventclient.so",
    "/etc/sys_version.conf:/etc/sys_version.conf",
    "/usr/lib64/libmpi_dvpp_adapter.so:/usr/lib64/libmpi_dvpp_adapter.so",
    "/usr/lib64/libascend_hal.so:/usr/lib64/libascend_hal.so",
    "/var/dmp_daemon:/var/dmp_daemon",
    "/usr/local/Ascend/driver:/usr/local/Ascend/driver",
    "/usr/lib64/libslog.so:/usr/lib64/libslog.so",
    "/usr/local/Ascend/ascend-toolkit:/usr/local/Ascend/ascend-toolkit",
    "/usr/lib64/libaicpu_sharder.so:/usr/lib64/libaicpu_sharder.so",
    "/usr/lib64/libdcmi.so:/usr/lib64/libdcmi.so"
};


// TestSuiteName, TestName
TEST_P(DockerClientTest, CreateContainer) {
    // cout << "[Test Begin] " << GetParam().create_container_param <<"----------------------------------" << endl;
    TestCreateContainer();
    TestStartContainer();
    TestPauseContainer();
    TestUnpauseContainer();
    // 我们不使用stop接口 会导致下面的kill接口报错  contianer is not running
    // TestStopContainer();
    TestKillContainer();
    TestRemoveContainer();
    cout << endl <<endl;
}


// 各个镜像参数输入

const DockerClientTestParam yolov_param = DockerClientTestParam{
    host, port, docker_version, 20,
    CreateContainerParam{"remote-yolov5", "yolov5-infer-cpp", vector<std::string>{},vector<std::string>{},true,
                              yolov_host_config_envs, yolov_host_config_binds,yolov_host_config_devices, "0.0.0.0", 5000,
                              5000 , true, "",}
};

const DockerClientTestParam yolov_param2 = DockerClientTestParam{
    host, port, docker_version, 20,
    CreateContainerParam{"remote-yolov5-2", "yolov5-infer-cpp", vector<std::string>{},vector<std::string>{},true,
                              yolov_host_config_envs, yolov_host_config_binds,yolov_host_config_devices, "0.0.0.0", 5000,
                              5000 , true, "",}
};



//atlas model test
const DockerClientTestParam bert_param = DockerClientTestParam{
    host, port, docker_version, 20,
    CreateContainerParam{"bert", "bert", vector<std::string>{},vector<std::string>{},true,
                              yolov_host_config_envs, yolov_host_config_binds,yolov_host_config_devices, "0.0.0.0", 6000,
                              6000 , true, "",}
};
const DockerClientTestParam resnet50_param = DockerClientTestParam{
    host, port, docker_version, 20,
    CreateContainerParam{"resnet50", "resnet50", vector<std::string>{},vector<std::string>{},true,
                              yolov_host_config_envs, yolov_host_config_binds,yolov_host_config_devices, "0.0.0.0", 5000,
                              5000 , true, "",}
};
const DockerClientTestParam deeplabv3_param = DockerClientTestParam{
    host, port, docker_version, 20,
    CreateContainerParam{"deeplabv3", "deeplabv3", vector<std::string>{},vector<std::string>{},true,
                              yolov_host_config_envs, yolov_host_config_binds,yolov_host_config_devices, "0.0.0.0", 6001,
                              6001 , true, "",}
};
const DockerClientTestParam mobilenetv3_param = DockerClientTestParam{
    host, port, docker_version, 20,
    CreateContainerParam{"mobilenetv3", "mobilenetv3", vector<std::string>{},vector<std::string>{},true,
                              yolov_host_config_envs, yolov_host_config_binds,yolov_host_config_devices, "0.0.0.0", 5001,
                              5001 , true, "",}
};
const DockerClientTestParam transcoding_param = DockerClientTestParam{
    host, port, docker_version, 20,
    CreateContainerParam{"transcoding", "transcoding", vector<std::string>{},vector<std::string>{},true,
                              yolov_host_config_envs, yolov_host_config_binds,yolov_host_config_devices, "0.0.0.0", 8554,
                              8554 , true, "",}
};

INSTANTIATE_TEST_SUITE_P(YOLOV5, DockerClientTest,
                         testing::Values(
                             yolov_param,
                             yolov_param2
                         ));

INSTANTIATE_TEST_SUITE_P(BERT, DockerClientTest, testing::Values(
    bert_param
));

INSTANTIATE_TEST_SUITE_P(ResNet50, DockerClientTest, testing::Values(
    resnet50_param  
));

INSTANTIATE_TEST_SUITE_P(DeepLabV3, DockerClientTest, testing::Values(
    deeplabv3_param
));
INSTANTIATE_TEST_SUITE_P(MobileNetV3, DockerClientTest, testing::Values(
    mobilenetv3_param
));
INSTANTIATE_TEST_SUITE_P(Transcoding, DockerClientTest, testing::Values(
    transcoding_param
));


namespace RK3588 {
    const string host = "192.168.58.3";
    const int port = 2375;
    const string docker_version = "v1.45";
    const int read_timeout_sec = 20;

    const DockerClientTestParam yolov5_param = DockerClientTestParam{
            host, port, docker_version, read_timeout_sec,
            CreateContainerParam{"yolov5_infer_rk3588", "yolov5_infer_rk3588", {}, {}, true, {}, {}, {}, "0.0.0.0",
                                 6000, 6000, true, "",}
    };

    const DockerClientTestParam resnet_param = DockerClientTestParam{
            host, port, docker_version, read_timeout_sec,
            CreateContainerParam{"resnet_infer_rk3588", "resnet_infer_rk3588", {}, {}, true, {}, {}, {}, "0.0.0.0",
                                 6000, 6000, true, "",}
    };

    const DockerClientTestParam mobilenet_param = DockerClientTestParam{
            host, port, docker_version, read_timeout_sec,
            CreateContainerParam{"mobilenet_infer_rk3588", "mobilenet_infer_rk3588", {}, {}, true, {}, {}, {},
                                 "0.0.0.0", 6000, 6000, true, "",}
    };
}

INSTANTIATE_TEST_SUITE_P(RK3588_YOLOV5, DockerClientTest, testing::Values(
        RK3588::yolov5_param
));
INSTANTIATE_TEST_SUITE_P(RK3588_ResNet50, DockerClientTest, testing::Values(
        RK3588::resnet_param
));
INSTANTIATE_TEST_SUITE_P(RK3588_MobileNetV3, DockerClientTest, testing::Values(
        RK3588::mobilenet_param
));
