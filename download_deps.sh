#!/bin/bash

# 设置下载目录
DEPS_DIR="3rdparty"
mkdir -p "$DEPS_DIR"
cd "$DEPS_DIR" || exit

echo "正在准备下载依赖包到 $(pwd) ..."

# 定义函数：下载、解压并重命名
# 参数 1: URL
# 参数 2: 目标目录名 (例如 googletest-src)
# 参数 3: 解压后的原始目录名模式 (用于 mv，例如 googletest-*)
download_and_extract() {
    url="$1"
    target_name="$2"
    # 这里只是一个简单的猜测模式，通常 GitHub 的 zip 解压后是 RepoName-Tag
    # 我们会在解压后动态获取解压出的文件夹名

    echo "------------------------------------------------"
    echo "处理: $target_name"

    if [ -d "$target_name" ] && [ "$(ls -A "$target_name")" ]; then
        echo "目录 $target_name 已存在且非空，跳过。"
        return
    fi

    # 目录存在但为空时，清理后重新下载
    rm -rf "$target_name"

    filename="${target_name}.zip"

    # 下载
    echo "正在下载: $url"
    if command -v wget >/dev/null 2>&1; then
        wget -q --show-progress -O "$filename" "$url"
    elif command -v curl >/dev/null 2>&1; then
        curl -L -o "$filename" "$url"
    else
        echo "错误: 未找到 wget 或 curl，请先安装。"
        exit 1
    fi

    if [ ! -f "$filename" ]; then
        echo "错误: 下载失败 $url"
        return
    fi

    # 解压
    echo "正在解压..."
    unzip -q "$filename"

    # 获取解压出来的文件夹名称（假设是 zip 里包含的唯一顶级目录）
    # 排除 __MACOSX 这种垃圾文件
    extracted_dir=$(zipinfo -1 "$filename" | head -n 1 | cut -d/ -f1)

    if [ -d "$extracted_dir" ]; then
        echo "重命名 $extracted_dir -> $target_name"
        mv "$extracted_dir" "$target_name"
        rm "$filename"
    else
        echo "错误: 解压后未找到预期目录，请手动检查 $filename"
    fi
}

# --- 开始下载列表 (基于您的 CMakeLists.txt) ---

# 1. GoogleTest (指定 commit ID)
download_and_extract \
    "https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip" \
    "googletest-src"

# 2. cpp-httplib (v0.18.0)
download_and_extract \
    "https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.0.zip" \
    "httplib-src"

# 3. nlohmann/json (v3.11.3)
download_and_extract \
    "https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip" \
    "json-src"

# 4. spdlog (v1.15.0)
download_and_extract \
    "https://github.com/gabime/spdlog/archive/refs/tags/v1.15.0.zip" \
    "spdlog-src"

# 5. Boost 子模块 (boost-1.86.0)
download_and_extract \
    "https://github.com/boostorg/uuid/archive/refs/tags/boost-1.86.0.zip" \
    "boost-uuid-src"

download_and_extract \
    "https://github.com/boostorg/assert/archive/refs/tags/boost-1.86.0.zip" \
    "boost-assert-src"

download_and_extract \
    "https://github.com/boostorg/config/archive/refs/tags/boost-1.86.0.zip" \
    "boost-config-src"

download_and_extract \
    "https://github.com/boostorg/throw_exception/archive/refs/tags/boost-1.86.0.zip" \
    "boost-throw-src"

download_and_extract \
    "https://github.com/boostorg/type_traits/archive/refs/tags/boost-1.86.0.zip" \
    "boost-traits-src"

download_and_extract \
    "https://github.com/boostorg/static_assert/archive/refs/tags/boost-1.86.0.zip" \
    "boost-static-src"

echo "------------------------------------------------"
echo "所有依赖已下载至 $(pwd)"
