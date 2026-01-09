#!/bin/bash

# 获取脚本所在目录，确保在项目根目录执行
PROJECT_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$PROJECT_ROOT"

echo "============================================"
echo "   cleaning..."
echo "   path: $PROJECT_ROOT"
echo "============================================"

# 1. 清理 CMake 构建目录
# 根据 .gitignore 规则: build/, cmake-build*/, _deps/, src/_deps/
echo "[1/4] 删除 CMake 构建目录..."
rm -rf build
rm -rf cmake-build-*
rm -rf _deps
rm -rf src/_deps

# 2. 递归清理源码目录中的 CMake 中间文件
# 防止源码内构建 (in-source build) 残留
# 根据 .gitignore 规则: src/CMakeFiles
echo "[2/4] 递归清理 CMake 缓存文件..."
find . -name "CMakeFiles" -type d -exec rm -rf {} +
find . -name "CMakeCache.txt" -type f -delete
find . -name "cmake_install.cmake" -type f -delete
find . -name "Makefile" -type f -delete
find . -name "CTestTestfile.cmake" -type f -delete
find . -name "*.cbp" -type f -delete

# 3. 清理 Python 缓存
# 根据 .gitignore 规则: src/modules/slave/task_dealer/可执行程序/yolov5-ascend/__pycache__/
echo "[3/4] 清理 Python 缓存 (__pycache__)..."
find . -name "__pycache__" -type d -exec rm -rf {} +
find . -name "*.pyc" -type f -delete

# 4. 清理 IDE 配置目录 (可选，默认不删除，如果需要请取消注释)
# 根据 .gitignore 规则: .vscode, .idea
# echo "[4/4] 清理 IDE 配置..."
rm -rf .idea
rm -rf .vscode

echo "============================================"
echo "clean completed"
echo "============================================"
