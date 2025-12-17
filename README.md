# lite edge cluster scheduler

## TODO
动态地址初始化 master ip 以参数在编译中传入  
统一模型输入/输出路径  
检查模型推理内存泄漏问题  


## 配置\&构建

### 配置
for Atlas 310
```sh
cmake -S . -B build -DAGENT_DEVICE_TYPE=ATLAS_I
```

### 构建
编译源代码(4线程)
```sh
cmake --build build -j 8
```

## TOTOAL WORKFLOW
1.master-task_manager
python3 ./src/modules/master/task_manager.py --port 9999 --strategy schedule --upload_path=workspace/master/data/upload

listen 9999
请求master-gateway路由http://127.0.0.1:6666/{self.strategy}
2.master-gateway
 ./build/src/gateway/gateway --config ./config_files --task workspace/master/data/upload

--task upload pic path

6666 port register_node .et details of urls in HttpServer.h
7777 port sockerServer is dropped now,it's deal task quest specificly

3.slave-agent
./build/src/docker_scheduler_agent/docker_scheduler_agent --master-ip 127.0.0.1 --master-port 6666  --disconnect 100000 --reconnect 20  --bandwidth-fluctuate

bandwidth-fluctuate exist means ture,net fluctuate  ,otherwise it doesn't exist means steay

4.slave-recv_server
python3 src/modules/slave/recv_server.py
5.slave-rst_sender
python3 ./src/modules/slave/rst_send.py --input-dir workspace/slave/data/output/label --interval 10

input-dir= yolo output path
interval= scan output dir’s interval
6.task_producer-rst_recv
python3 ./src/modules/client/rst_recv.py --port 8888 --dir workspace/client/data/rst
7.task_producer-pic_send

python3 ./src/modules/client/send_pic.py --host=127.0.0.1 --port=9999 --dir=workspace/client/data/req --max=200 --workers=8

-max pics'count
-workers send threads'count
-host ip of master-task_manager's host 
--port master:task_manager listen port