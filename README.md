# docker-scheduler
nku docker-scheduler

#docker API

docker接口文档:https://docs.docker.com/reference/api/engine/version/v1.47/

docker的sdk文档:https://docs.docker.com/reference/api/engine/

# 第三方库
http库 https://github.com/yhirose/cpp-httplib

json库 https://github.com/nlohmann/json/

# 目前工作

## 实现接口
### Container

```
// 详细接口、参数返回值 参考函数定义上注解
CreateContainer
StartContainer
StopContainer
PauseContainer
UnpauseContainer
```

## 修改docker默认配置使其可以被外部访问
```
// 编辑启动项

vi /usr/lib/systemd/system/docker.service

// 将启动配置文件添加如下选项
ExecStart=/usr/bin/dockerd -H tcp://0.0.0.0:2375 -H unix:///var/run/docker.sock

// 重启docker
systemctl daemon-reload
systemctl restart docker

// 使用下面命令开启防火墙
firewall-cmd --permanent --add-port=2375/tcp
firewall-cmd --reload
firewall-cmd --list-ports // 验证
可以查看所有的镜像
curl --unix-socket /var/run/docker.sock http://localhost:2375/v1.44/images/json
curl 192.168.134.144:2375/v1.44/images/json
```

## 其他
### Docker 启动yolov5命令
```
docker run -itd --privileged \
-e ASCEND_VISIBLE_DEVICES=0 \
-e ASCEND_ALLOW_LINK=True \
--device=/dev/svm0 \
--device=/dev/ts_aisle \
--device=/dev/upgrade \
--device=/dev/sys \
--device=/dev/vdec \
--device=/dev/vpc \
--device=/dev/pngd \
--device=/dev/venc \
--device=/dev/dvpp_cmdlist \
--device=/dev/log_drv \
-v /etc/sys_version.conf:/etc/sys_version.conf \
-v /etc/hdcBasic.cfg:/etc/hdcBasic.cfg \
-v /usr/local/sbin/npu-smi:/usr/local/sbin/npu-smi \
-v /usr/local/Ascend/driver/lib64:/usr/local/Ascend/driver/lib64 \
-v /usr/lib64/aicpu_kernels/:/usr/lib64/aicpu_kernels/ \
-v /var/slogd:/var/slogd \
-v /var/dmp_daemon:/var/dmp_daemon \
-v /usr/lib64/libaicpu_processer.so:/usr/lib64/libaicpu_processer.so \
-v /usr/lib64/libaicpu_prof.so:/usr/lib64/libaicpu_prof.so \
-v /usr/lib64/libaicpu_sharder.so:/usr/lib64/libaicpu_sharder.so \
-v /usr/lib64/libadump.so:/usr/lib64/libadump.so \
-v /usr/lib64/libtsd_eventclient.so:/usr/lib64/libtsd_eventclient.so \
-v /usr/lib64/libaicpu_scheduler.so:/usr/lib64/libaicpu_scheduler.so \
-v /usr/lib64/libdcmi.so:/usr/lib64/libdcmi.so \
-v /usr/lib64/libmpi_dvpp_adapter.so:/usr/lib64/libmpi_dvpp_adapter.so \
-v /usr/lib64/libstackcore.so:/usr/lib64/libstackcore.so \
-v /usr/local/Ascend/driver:/usr/local/Ascend/driver \
-v /etc/ascend_install.info:/etc/ascend_install.info \
-v /var/log/ascend_seclog:/var/log/ascend_seclog \
-v /var/davinci/driver:/var/davinci/driver \
-v /usr/lib64/libc_sec.so:/usr/lib64/libc_sec.so \
-v /usr/lib64/libdevmmap.so:/usr/lib64/libdevmmap.so \
-v /usr/lib64/libdrvdsmi.so:/usr/lib64/libdrvdsmi.so \
-v /usr/lib64/libslog.so:/usr/lib64/libslog.so \
-v /usr/lib64/libmmpa.so:/usr/lib64/libmmpa.so \
-v /usr/lib64/libascend_hal.so:/usr/lib64/libascend_hal.so \
-v /usr/local/Ascend/ascend-toolkit:/usr/local/Ascend/ascend-toolkit \
--name yolov5-infer \
-p 8080:80 \
yolov5-infer
```
### curl发送测试请求
```
http://ip:8080/predict，请求体为form-data，key为image，value为文件
curl -F "image=@/root/lxs/soccer.jpg" -X POST localhost:8080/predict

curl -F "key=@[文件地址]" -F "key=value" -X POST https://reqbin.com/echo/post
```


### 编译

```
mkdir build
cd build
cmake .. -DAGENT_DEVICE_TYPE=ATLAS_I
make -j8
```

### slave 节点启动项

添加了命令行参数解析功能，支持以下参数：
--disconnect <seconds>: 设置自动断连时间（默认 30 秒）参数小于等于0禁用自动断连
--reconnect <seconds>: 设置自动重连时间（默认 20 秒）
--bandwidth-fluctuate: 开启网络带宽波动模拟
--help: 显示帮助信息

```
# 默认参数运行
./build/src/docker_scheduler_agent/docker_scheduler_agent

# 自定义断连时间为60秒，重连时间为10秒
./build/src/docker_scheduler_agent/docker_scheduler_agent --disconnect 60 --reconnect 10

# 开启带宽波动
./build/src/docker_scheduler_agent/docker_scheduler_agent --bandwidth-fluctuate

# 组合使用
./build/src/docker_scheduler_agent/docker_scheduler_agent --disconnect 45 --reconnect 15 --bandwidth-fluctuate
```
