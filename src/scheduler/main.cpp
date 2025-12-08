#include <DockerClient.h>
#include <TimeRecorder.h>
#include"scheduler.h"
using json = nlohmann::json;
const std::vector<std::string>  yolov_host_config_envs= {
   "CEND_VISIBLE_DEVICES=0",
   "ASCEND_ALLOW_LINK=True"
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



int main() {
    // Docker_scheduler scheduler;
    // httplib::Server schedule_svr;
    //
    // int dev_type;
    // string newIP;
    // int newPort;
    // //schedule_svr listning the request from all of othe agent
    //
    // scheduler.RegisNode(dev_type,newIP,newPort);
    // scheduler.display_dev();
    // scheduler.display_devinfo();
    // return 0;
}
