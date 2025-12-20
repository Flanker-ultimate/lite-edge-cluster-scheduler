# 边缘集群调度系统 (Edge Cluster Scheduler)

一个轻量级的边缘集群调度系统，支持在多个异构设备(Atlas、RK3588、Orin等)间智能分配AI推理任务。

## 🌟 特性

- **多设备异构支持**: 支持 Atlas 310、RK3588、Orin 等多种边缘计算设备
- **智能调度算法**:
  - 负载贪心算法：基于设备CPU、内存、XPU使用率和网络带宽的加权评分
  - 轮询调度：简单公平的轮询分配策略
- **故障恢复**: 任务失败自动重试，支持重试队列机制
- **心跳检测**: 设备健康状态自动监控
- **动态扩容**: 支持设备动态接入和退出

## 🏗️ 系统架构

### 组件说明

1. **master-task_manager** [端口9999]
   - gRPC服务器，接收来自客户端的任务请求
   - 管理任务创建和分发
   - 支持轮询和负载贪心两种调度策略供选择

2. **master-gateway** [端口6666]
   - HTTP API网关
   - 路由：`/schedule`（支持策略参数：`?stargety=load`|[负载贪心]<br>`?stargety=roundrobin`）[轮询]
   - 支持动态任务调度

3. **slave-agent** [连接master:6666]
   - 设备代理程序，定期上报设备状态
   - 支持网络带宽波动模拟

4. **slave-recv_server** [端口20810]
   - 任务接收服务器
   - 处理来自master的任务分发

5. **slave-rst_sender**
   - 结果发送器
   - 定期扫描输出目录并发送结果

## 🚀 快速开始

### 🔧 构建与配置

#### 环境要求
- C++ 17
- Python 3.7+
- CMake 3.14+

#### 设备类型配置

```bash
# 针对 Atlas 310
cmake -S . -B build -DAGENT_DEVICE_TYPE=ATLAS_I

# 针对 RK3588
cmake -S . -B build -DAGENT_DEVICE_TYPE=RK3588

# 针对 Orin
cmake -S . -B build -DAGENT_DEVICE_TYPE=ORIN
```

#### 编译
```bash
cmake --build build -j 8
```

## 📋 使用说明

### 0 清除旧数据
```bash
# client：清空请求/结果目录
rm -rf workspace/client/data/*/rst/*

# slave：清空各 service 的输入/输出与日志
rm -rf workspace/slave/data/*/input/*
rm -rf workspace/slave/data/*/output/*
rm -rf workspace/slave/log/*

# master：清空上传目录（task_manager 落盘）
rm -rf workspace/master/data/upload/*

# 【可选】清空 master/gateway 运行日志目录（如启用了文件日志）
rm -rf workspace/master/log/*

# 【可选】重置 agent 设备 ID（将重新生成 `.agent_config.json`）
# rm -f .agent_config.json
```

### 1️⃣ 启动任务管理器（Task Manager）
```bash
# 使用推荐的直观参数
python3 ./src/modules/master/task_manager.py \
    --port 9999 \
    --strategy load \
    --upload_path=workspace/master/data/upload

# 或者使用轮询策略
python3 ./src/modules/master/task_manager.py \
    --port 9999 \
    --strategy roundrobin \
    --upload_path=workspace/master/data/upload
```
**参数说明：**
- `-p/--port`: gRPC监听端口（默认：9999）
- `-s/--strategy`: 调度策略（直接对应网关查询参数）
  - `load`: 负载贪心策略（对应 `?stargety=load`） - 基于设备负载的智能调度
  - `roundrobin`: 轮询策略（对应 `?stargety=roundrobin`） - 公平的轮询分配
- `-u/--upload_path`: 图片上传目录

### 2️⃣ 启动调度网关（Gateway）
```bash
./build/src/gateway/gateway \
    --config ./config_files \
    --task workspace/master/data/upload
```

**HTTP API:**  `http://127.0.0.1:6666`

默认行为：当收到 `POST /task_completed` 且 `status=success` 时，gateway 会 best-effort 删除 `--task` 目录下对应的上传文件（`<client_ip>/<filename>`），防止目录无限增长。若希望保留上传文件用于排查，可加 `--keep-upload`。

**服务迁移（任务重新分发）**
- gateway 会周期检测 slave 上报的 `net_latency`，当延迟超过 10s 时，会将该 slave 上“已分发但未处理完”的任务从运行队列取出并重新加入 pending 队列等待再次调度

### 3️⃣ 启动设备代理（Agent）
```bash
./build/src/docker_scheduler_agent/docker_scheduler_agent \
    --master-ip 127.0.0.1 \
    --master-port 6666 \
    --disconnect 100000 \
    --reconnect 20 \
    --bandwidth-fluctuate
```
agent 默认会在注册成功后启动并守护 `slave-recv_server` 与 `slave-rst_sender`。
- 可通过 `--no-manage-services` 关闭（此时需手动启动 `recv_server.py`/`rst_send.py`）。
- 启动命令通过 `config_files/agent_services.json` 配置（会替换 `{DEVICE_ID}`/`{MASTER_IP}`/`{MASTER_PORT}`/`{PYTHON}`）。
- agent 会将 `agent_services.json` 中的 `autostart_services` 上报给 master，用于让 scheduler 优先调度到“已启动对应服务”的节点。
**参数说明：**
- `--bandwidth-fluctuate`: 启用网络带宽波动模拟
- `--disconnect`: 断开重连间隔（秒）
- `--reconnect`: 重试间隔（秒）

### 4【可选】启动接收服务器（Receive Server）
```bash
python3 src/modules/slave/recv_server.py --config config_files/slave_backend.json
```
说明：默认不需要手动启动（由 Agent 负责启动/守护）。只有在你使用 `--no-manage-services` 关闭 Agent 管理时才需要执行本步骤。

**参数说明：**
- `--config`：`slave_backend.json` 路径（默认读取 `config_files/slave_backend.json`）
- `--agent-port`：agent 控制端口（用于 `POST /ensure_service`，默认 8000）

recv_server 会读取 `config_files/slave_backend.json`，按服务(tasktype)落盘到 `input_dir/<ip>/...`；后端（binary/container）的启动/守护统一由 agent 负责。
前提：`tasktype` 与 `service` 名字完全一致（大小写也一致），即“一个 tasktype 对应唯一一个 service”。

#### `slave_backend.json` 字段说明（核心）
- `services.<ServiceName>.backend`: 后端类型
  - `binary`: 由 `agent` 启动/守护 `start_cmd` 指定的可执行程序（需支持循环处理输入目录并持续输出）
  - `container`: 由 `agent` 启动/守护 `start_cmd` 指定的容器启动命令（容器内需按 `INPUT_DIR/OUTPUT_DIR` 约定循环处理）
  - `local`: 仅落盘，不负责启动后端（你可自行在 slave 上启动对应进程）
- `services.<ServiceName>.agent_autostart`: 是否在 agent 启动时就同步启动该 service 的后端（默认按需在首次收到任务时启动）
- `services.<ServiceName>.input_dir`: 该 service 的输入根目录；实际任务文件会写入 `input_dir/<client_ip>/<filename>`
- `services.<ServiceName>.output_dir`: 该 service 的输出根目录（由后端写入处理结果）
- `services.<ServiceName>.result_dir`: `rst_send` 扫描并回传结果的目录（通常是 `output_dir/label`）
- `services.<ServiceName>.start_cmd`: 当 `backend` 为 `binary/container` 时必填，支持占位符 `${INPUT_DIR}`、`${OUTPUT_DIR}`、`${SERVICE_NAME}`

#### slave 侧目录约定（推荐）
- `workspace/slave/data/<ServiceName>/input/<client_ip>/...`：recv_server 落盘的输入
- `workspace/slave/data/<ServiceName>/output/...`：后端输出根目录（result_dir 在此目录下的某个子目录）

#### slave 侧日志约定
- `workspace/slave/log/agent.log`：agent 日志（注册、ensure_service、进程启动/重启等）
- `workspace/slave/log/recv_server.log`：recv_server 日志
- `workspace/slave/log/rst_send.log`：rst_send 日志
- `workspace/slave/log/<ServiceName>/service.log`：该 service 后端处理器日志（binary/container 的 stdout/stderr）

### 5【可选】启动结果发送器（Result Sender）
```bash
python3 ./src/modules/slave/rst_send.py \
    --config config_files/slave_backend.json \
    --input-dir workspace/slave/data \
    --interval 10 \
    --target-port 8888 \
    --gateway-host 127.0.0.1 \
    --gateway-port 6666 \
    --device-id slave-1
```
说明：默认不需要手动启动（由 Agent 负责启动/守护）。只有在你使用 `--no-manage-services` 关闭 Agent 管理时才需要执行本步骤。

**参数说明：**
- `--config`：`slave_backend.json` 路径（用于读取各 service 的 `result_dir`，并在回传时携带 `service` 字段）
- `--service`：只发送指定 service 的结果（例如 `--service YoloV5`），不传则发送全部 service
- `--input-dir`：兼容单目录模式的扫描根目录（当 `--config` 不可用/未配置 services 时，按 `<input-dir>/<client_ip>/...` 扫描）
- `--interval/-t`：扫描间隔（秒）
- `--target-port/-p`：client 侧 `rst_recv` 监听端口（默认 8888）
- `--gateway-host/--gateway-port`：master-gateway 地址（用于 `POST /task_completed` 通知）
- `--device-id`：上报给 gateway 的节点 ID（通常由 agent 注入 `{DEVICE_ID}`）

### 6️⃣ 启动客户端接收器（Client Receiver）
```bash
python3 ./src/modules/client/rst_recv.py \
    --port 8888 \
    --dir workspace/client/data
```
client receiver 会按 `workspace/client/data/<ServiceName>/rst/...` 存放结果（例如 `workspace/client/data/YoloV5/rst/...`）。
如果你只跑单一服务，也可以加 `--tasktype YoloV5` 作为缺省目录（当请求未携带 `service` 字段时生效）。

### 7️⃣ 启动任务发送器（Task Sender）
```bash
python3 ./src/modules/client/req_send.py \
    --host=127.0.0.1 \
    --port=9999 \
    --tasktype=YoloV5 \
    --max=200 \
    --workers=8
```
说明：`--tasktype` 用于指定该任务希望由哪个服务（服务名=tasktype）处理；master 会携带该字段转发，scheduler 会按 tasktype 选择支持该服务的 slave。

## ⚙️ 调度策略配置

系统支持两种调度策略，参数直接对应网关查询参数：

| Task Manager参数 | 网关查询参数 | 策略说明 |
|-----------------|--------------|----------|
| `load` | `?stargety=load` | **负载贪心（默认）** - 基于设备负载的智能调度，考虑CPU、内存、XPU使用率和网络带宽 |
| `roundrobin` | `?stargety=roundrobin` | **轮询调度** - 公平的轮询分配，适用于负载均衡场景 |

**注意事项**：
- 网关默认使用负载贪心策略（不指定参数时）

## 📁 项目结构

```
lite-edge-cluster-scheduler/
├── src/
│   ├── gateway/          # 调度网关（HTTP API）
│   ├── modules/
│   │   ├── master/      # 主控模块（Task Manager）
│   │   ├── slave/       # 从设备模块
│   │   └── client/      # 客户端模块
│   └── scheduler/       # 调度器核心
├── build/               # 构建输出目录
├── config_files/        # 配置文件
└── workspace/           # 工作区目录
    ├── master/
    ├── slave/
    └── client/
```

## 🔍 监控与调试

- **设备状态**：通过网关API可实时查看设备状态
- **任务日志**：各组件均输出详细日志信息
- **性能监控**：支持任务执行时间统计

## 📝 TODO

- [ ] 动态地址初始化：支持master IP作为编译参数传入
- [ ] 统一模型输入/输出路径
- [ ] 检查模型推理内存泄漏问题
- [ ] 支持更多设备类型
- [ ] 优化网络通信协议

## 🤝 贡献

欢迎提交 Issue 和 Pull Request 来帮助改进项目！

## 📄 许可证

[请在此处添加许可证信息]
