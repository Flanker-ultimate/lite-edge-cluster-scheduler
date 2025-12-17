#!/bin/bash

################################################################################
# Master Components Startup Script
#
# 流程说明：
# 1. 脚本位于项目目录下的 startup/ 子目录中
# 2. 执行脚本时创建项目根目录下的 ./workspace/master/upload 目录结构
# 3. 检查项目根目录下的 ../build 目录是否存在（相对于脚本位置）
#    - 如果不存在，则在项目根目录下执行 cmake --build build -j 8 重新编译
# 4. 启动两个 master 组件：
#    a. master-task_manager (Python server) - 可配置端口
#       - 命令：python task_manager.py --port <task_manager_port> --strategy schedule --upload_path=<workspace_path>
#    b. master-gateway (C++ binary) - 固定参数
#       - 固定命令：./gateway --config /home/ubuntu/lite-edge-cluster-scheduler/config_files --task /home/ubuntu
# 5. 监控进程状态，任一组件失败则停止所有组件
################################################################################

set -e  # 遇到错误立即退出

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 打印带颜色的消息
print_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

print_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# 显示用法
usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --task-manager-port <port> Task manager port (default: 9999)"
    echo "  --help                     Show this help message"
    echo ""
    echo "Note: Gateway uses fixed parameters:"
    echo "  ./gateway --config /home/ubuntu/lite-edge-cluster-scheduler/config_files --task /home/ubuntu"
    echo ""
    echo "Example:"
    echo "  $0                                    # Use default port 9999"
    echo "  $0 --task-manager-port 10001          # Use custom port"
}

# 获取项目根目录和脚本位置
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"  # 项目根目录是 startup 的父目录
BUILD_DIR="$PROJECT_ROOT/build"         # build 目录在项目根目录下
WORKSPACE_DIR="$PROJECT_ROOT/workspace"  # workspace 在项目根目录下
MASTER_UPLOAD_DIR="$WORKSPACE_DIR/master/upload"  # master upload 目录

# 默认配置
GATEWAY_CONFIG="./config_files"
GATEWAY_TASK="$WORKSPACE_DIR/master/upload/pics"
TASK_MANAGER_STRATEGY="schedule"
TASK_MANAGER_PORT="9999"  # 默认值，可通过参数配置

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --task-manager-port)
            TASK_MANAGER_PORT="$2"
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# 验证端口号
if ! [[ "$TASK_MANAGER_PORT" =~ ^[0-9]+$ ]] || [[ "$TASK_MANAGER_PORT" -lt 1 ]] || [[ "$TASK_MANAGER_PORT" -gt 65535 ]]; then
    print_error "Invalid task manager port: $TASK_MANAGER_PORT. Must be between 1-65535"
    exit 1
fi

print_info "Script location: $SCRIPT_DIR"
print_info "Project root: $PROJECT_ROOT"
print_info "Build directory: $BUILD_DIR"
print_info "Workspace directory: $WORKSPACE_DIR"
print_info "Master upload directory: $MASTER_UPLOAD_DIR"
print_info ""
print_info "Starting master components with configuration:"
print_info "Gateway: ./gateway --config $GATEWAY_CONFIG --task $GATEWAY_TASK (FIXED)"
print_info "Task Manager: python task_manager.py --port $TASK_MANAGER_PORT --strategy $TASK_MANAGER_STRATEGY --upload_path=$MASTER_UPLOAD_DIR"

# 创建工作空间目录结构（仅 master 相关）
print_info ""
print_info "Creating workspace directory structure (master only)..."
mkdir -p "$MASTER_UPLOAD_DIR"

if [[ ! -d "$WORKSPACE_DIR" ]]; then
    print_error "Failed to create workspace directory: $WORKSPACE_DIR"
    exit 1
fi

if [[ ! -d "$MASTER_UPLOAD_DIR" ]]; then
    print_error "Failed to create master upload directory: $MASTER_UPLOAD_DIR"
    exit 1
fi

print_success "Workspace structure created successfully:"
print_info "  Workspace: $WORKSPACE_DIR"
print_info "  Master Upload: $MASTER_UPLOAD_DIR"

# 显示目录结构
print_info ""
print_info "Workspace directory structure:"
print_info "  workspace/"
print_info "  └── master/"
print_info "      └── upload/"

# 检查并编译 build 目录
print_info ""
print_info "Checking build directory: $BUILD_DIR"

if [[ ! -d "$BUILD_DIR" ]]; then
    print_warning "Build directory not found: $BUILD_DIR"
    print_info "Starting compilation process in project root: $PROJECT_ROOT"
    
    # 检查项目根目录是否有 CMakeLists.txt
    if [[ ! -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
        print_error "CMakeLists.txt not found in project root: $PROJECT_ROOT"
        exit 1
    fi
    
    # 创建 build 目录并在项目根目录下编译
    cd "$PROJECT_ROOT"
    mkdir -p "$BUILD_DIR"
    
    print_info "Running cmake in $PROJECT_ROOT..."
    if ! cmake -B build -S .; then
        print_error "cmake failed!"
        exit 1
    fi
    
    print_info "Building project with 8 jobs..."
    if ! cmake --build build -j 8; then
        print_error "Build failed!"
        exit 1
    fi
    
    print_success "Build completed successfully"
else
    print_success "Build directory exists: $BUILD_DIR"
fi

# 检查必要的文件是否存在
print_info ""
print_info "Checking required files..."

# 检查 gateway 二进制文件（使用固定路径）
GATEWAY_BINARY="$PROJECT_ROOT/gateway"
if [[ ! -f "$GATEWAY_BINARY" ]]; then
    # 如果在项目根目录没找到，尝试在 build 目录中找
    GATEWAY_BINARY="$BUILD_DIR/gateway"
    if [[ ! -f "$GATEWAY_BINARY" ]]; then
        # 最后尝试在 build/src/gateway/ 中找
        GATEWAY_BINARY="$BUILD_DIR/src/gateway/gateway"
        if [[ ! -f "$GATEWAY_BINARY" ]]; then
            print_error "Gateway binary not found!"
            print_info "Searched locations:"
            print_info "  $PROJECT_ROOT/gateway"
            print_info "  $BUILD_DIR/gateway"
            print_info "  $BUILD_DIR/src/gateway/gateway"
            print_info "Available files in build directory:"
            find "$BUILD_DIR" -name "*gateway*" -type f 2>/dev/null | head -10 || echo "  No gateway files found"
            exit 1
        fi
    fi
fi
print_success "Gateway binary found: $GATEWAY_BINARY"

# 检查 gateway 配置目录
if [[ ! -d "$GATEWAY_CONFIG" ]]; then
    print_warning "Gateway config directory not found: $GATEWAY_CONFIG"
    print_info "Will attempt to start gateway anyway"
else
    print_success "Gateway config directory found: $GATEWAY_CONFIG"
fi

# 检查 gateway task 路径
if [[ ! -d "$GATEWAY_TASK" ]]; then
    print_warning "Gateway task path not found: $GATEWAY_TASK"
    print_info "Will attempt to start gateway anyway"
else
    print_success "Gateway task path found: $GATEWAY_TASK"
fi

# 检查 Python server 文件
SERVER_PYTHON=""
if [[ -f "$PROJECT_ROOT/task_manager.py" ]]; then
    SERVER_PYTHON="$PROJECT_ROOT/task_manager.py"
elif [[ -f "$PROJECT_ROOT/src/modules/master/task_manager.py" ]]; then
    SERVER_PYTHON="$PROJECT_ROOT/src/modules/master/task_manager.py"
else
    print_warning "task_manager.py not found in common locations"
    print_info "Will attempt to start with task_manager.py and let it fail if not found"
    SERVER_PYTHON="task_manager.py"  # 尝试当前目录
fi

if [[ -n "$SERVER_PYTHON" && -f "$SERVER_PYTHON" ]]; then
    print_success "Server script found: $SERVER_PYTHON"
else
    print_warning "task_manager.py not found, will attempt to start anyway"
    SERVER_PYTHON="task_manager.py"
fi

# 检查 task_manager.py 中是否使用了旧的 upload_path
if [[ -f "$SERVER_PYTHON" ]]; then
    if grep -q "src/module/master/files/pic" "$SERVER_PYTHON"; then
        print_warning "task_manager.py contains old upload path 'src/module/master/files/pic'"
        print_info "The script will use the new workspace path: $MASTER_UPLOAD_DIR"
    fi
fi

# 启动 master 组件
declare -a PIDS
declare -a LOG_FILES
declare -a COMPONENT_NAMES

# 清理函数
cleanup() {
    print_info "Cleaning up..."
    for i in "${!PIDS[@]}"; do
        pid=${PIDS[$i]}
        name=${COMPONENT_NAMES[$i]}
        if kill -0 "$pid" 2>/dev/null; then
            print_info "Stopping $name (PID: $pid)"
            kill "$pid" 2>/dev/null || true
        fi
    done
    # 等待进程结束
    sleep 2
    for i in "${!PIDS[@]}"; do
        pid=${PIDS[$i]}
        name=${COMPONENT_NAMES[$i]}
        if kill -0 "$pid" 2>/dev/null; then
            print_warning "Force killing $name (PID: $pid)"
            kill -9 "$pid" 2>/dev/null || true
        fi
    done
    print_success "Cleanup completed"
}

# 设置中断信号处理
trap cleanup EXIT INT TERM

# 确保在项目根目录执行后续操作
cd "$PROJECT_ROOT"

# 1. 启动 master-gateway (C++ binary) - 使用固定命令
print_info ""
print_info "Starting master-gateway with fixed command..."
print_info "Command: $GATEWAY_BINARY --config $GATEWAY_CONFIG --task $GATEWAY_TASK"

GATEWAY_LOG="$WORKSPACE_DIR/gateway.log"
GATEWAY_CMD="$GATEWAY_BINARY --config $GATEWAY_CONFIG --task $GATEWAY_TASK"

print_info "Gateway log: $GATEWAY_LOG"

if nohup $GATEWAY_CMD > "$GATEWAY_LOG" 2>&1 & then
    GATEWAY_PID=$!
    PIDS+=("$GATEWAY_PID")
    LOG_FILES+=("$GATEWAY_LOG")
    COMPONENT_NAMES+=("master-gateway")
    print_success "master-gateway started with PID: $GATEWAY_PID"
else
    print_error "Failed to start master-gateway!"
    exit 1
fi

# 等待 gateway 启动
sleep 3

# 检查 gateway 是否仍在运行
if ! kill -0 "$GATEWAY_PID" 2>/dev/null; then
    print_error "master-gateway process died unexpectedly!"
    print_info "Check gateway log: $GATEWAY_LOG"
    tail -20 "$GATEWAY_LOG" | sed 's/^/  /'
    exit 1
fi

print_success "master-gateway is running properly"

# 2. 启动 master-task_manager (Python server) - 使用可配置的端口
print_info ""
print_info "Starting master-task_manager with configurable port: $TASK_MANAGER_PORT"
print_info "Command: python $SERVER_PYTHON --port $TASK_MANAGER_PORT --strategy $TASK_MANAGER_STRATEGY --upload_path=$MASTER_UPLOAD_DIR"

TASK_MANAGER_LOG="$WORKSPACE_DIR/task_manager.log"

# 构建任务管理器命令（使用可配置的端口）
if [[ "$SERVER_PYTHON" == "task_manager.py" ]]; then
    TASK_MANAGER_CMD="python3 task_manager.py --port $TASK_MANAGER_PORT --strategy $TASK_MANAGER_STRATEGY --upload_path=$MASTER_UPLOAD_DIR"
else
    TASK_MANAGER_CMD="python3 $SERVER_PYTHON --port $TASK_MANAGER_PORT --strategy $TASK_MANAGER_STRATEGY --upload_path=$MASTER_UPLOAD_DIR"
fi

print_info "Task manager log: $TASK_MANAGER_LOG"

if nohup $TASK_MANAGER_CMD > "$TASK_MANAGER_LOG" 2>&1 & then
    TASK_MANAGER_PID=$!
    PIDS+=("$TASK_MANAGER_PID")
    LOG_FILES+=("$TASK_MANAGER_LOG")
    COMPONENT_NAMES+=("master-task_manager")
    print_success "master-task_manager started with PID: $TASK_MANAGER_PID (Port: $TASK_MANAGER_PORT)"
else
    print_error "Failed to start master-task_manager! Stopping gateway..."
    kill "$GATEWAY_PID" 2>/dev/null || true
    exit 1
fi

# 等待 task manager 启动
sleep 3

# 检查 task manager 是否仍在运行
if ! kill -0 "$TASK_MANAGER_PID" 2>/dev/null; then
    print_error "master-task_manager process died unexpectedly!"
    print_info "Check task manager log: $TASK_MANAGER_LOG"
    tail -20 "$TASK_MANAGER_LOG" | sed 's/^/  /'
    print_info "Stopping gateway..."
    kill "$GATEWAY_PID" 2>/dev/null || true
    exit 1
fi

print_success "master-task_manager is running properly"

# 保存进程信息
echo "$GATEWAY_PID" > "$WORKSPACE_DIR/gateway.pid"
echo "$TASK_MANAGER_PID" > "$WORKSPACE_DIR/task_manager.pid"

print_success "Both master components started successfully!"
print_info ""
print_info "Process Information:"
print_info "  Gateway PID: $GATEWAY_PID"
print_info "  Task Manager PID: $TASK_MANAGER_PID (Port: $TASK_MANAGER_PORT)"
print_info ""
print_info "Log Files:"
for log_file in "${LOG_FILES[@]}"; do
    print_info "  $log_file"
done
print_info ""
print_info "Workspace: $WORKSPACE_DIR"
print_info "Master Upload Directory: $MASTER_UPLOAD_DIR"
print_info ""
print_info "Component Details:"
print_info "  1. master-gateway (FIXED PARAMETERS):"
print_info "     - Command: $GATEWAY_BINARY --config $GATEWAY_CONFIG --task $GATEWAY_TASK"
print_info "     - Config: $GATEWAY_CONFIG"
print_info "     - Task Path: $GATEWAY_TASK"
print_info "  2. master-task_manager (CONFIGURABLE PORT):"
print_info "     - Command: python $SERVER_PYTHON --port $TASK_MANAGER_PORT --strategy $TASK_MANAGER_STRATEGY --upload_path=$MASTER_UPLOAD_DIR"
print_info "     - Port: $TASK_MANAGER_PORT (configurable via --task-manager-port)"
print_info "     - Strategy: $TASK_MANAGER_STRATEGY"
print_info "     - Upload Path: $MASTER_UPLOAD_DIR"

# 监控进程状态
print_info ""
print_info "Monitoring component status (press Ctrl+C to stop)..."
print_info "================================================================================"

while true; do
    sleep 10
    
    # 检查所有进程
    all_running=true
    for i in "${!PIDS[@]}"; do
        pid=${PIDS[$i]}
        log_file=${LOG_FILES[$i]}
        component_name=${COMPONENT_NAMES[$i]}
        
        if ! kill -0 "$pid" 2>/dev/null; then
            print_error "$component_name (PID: $pid) has stopped!"
            print_info "Last few lines of log ($log_file):"
            tail -10 "$log_file" | sed 's/^/  /'
            all_running=false
        fi
    done
    
    if [[ "$all_running" == false ]]; then
        print_error "One or more components have failed! Initiating shutdown..."
        exit 1
    fi
    
    # 显示运行状态
    gateway_status=$(kill -0 "$GATEWAY_PID" 2>/dev/null && echo "RUNNING" || echo "STOPPED")
    task_status=$(kill -0 "$TASK_MANAGER_PID" 2>/dev/null && echo "RUNNING" || echo "STOPPED")
    print_info "Status - Gateway: $gateway_status ($GATEWAY_PID), TaskManager: $task_status ($TASK_MANAGER_PID) [Port: $TASK_MANAGER_PORT]"
done