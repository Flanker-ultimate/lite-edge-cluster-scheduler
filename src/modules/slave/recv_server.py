#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
import os
import threading
import time
import http.client
import argparse
from concurrent.futures import ThreadPoolExecutor

from flask import Flask, jsonify, request

# 基本配置 - 使用固定路径，不再使用环境变量
CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))

# 直接计算项目根目录（向上两级）
PROJECT_ROOT = os.path.abspath(os.path.join(CURRENT_DIR, "../../.."))

# 使用简洁的路径结构
WORKSPACE_DIR = os.path.join(PROJECT_ROOT, "workspace", "slave")
DATA_ROOT = os.path.join(WORKSPACE_DIR, "data")
LOG_DIR = os.path.join(WORKSPACE_DIR, "log")

COUNT_LOG_FILE = os.path.join(LOG_DIR, "receive_stats.log")

# 其他配置
AGENT_PORT = 20810
ALLOWED_EXTS = {".jpg", ".jpeg", ".png", ".tif", ".tiff"}

# 创建必要的目录
for path in [DATA_ROOT, LOG_DIR]:
    os.makedirs(path, exist_ok=True)

EXECUTOR = ThreadPoolExecutor(max_workers=4)

# 接收计数（进程级），与阈值
RECV_LOCK = threading.Lock()
RECV_COUNT = 0
RECV_LOG_INTERVAL = 500  # 每收到 500 张记录一次时间戳

app = Flask(__name__)

_SLAVE_BACKEND_CONFIG_PATH = ""
_AGENT_CONTROL_PORT = 8000


# ===== 统一返回（失败也返回 200） =====
def build_success(result):
    return jsonify({"status": "success", "result": result}), 200


def build_failed(error):
    payload = {"error": error} if isinstance(error, str) else error
    # 返回非 200，便于 master 端（scheduler）识别失败并重试/换节点
    return jsonify({"status": "failed", "result": payload}), 500


@app.errorhandler(Exception)
def on_exception(e):
    return build_failed(str(e))


# ===== 工具函数 =====
def ensure_dir(path: str):
    os.makedirs(path, exist_ok=True)


def _agent_port() -> int:
    return int(_AGENT_CONTROL_PORT)


def _normalize_tasktype(raw) -> str:
    if not isinstance(raw, str):
        return "Unknown"
    s = raw.strip()
    if not s:
        return "Unknown"
    # 前提：tasktype == service name，完全一致（大小写也一致）
    # 兼容：避免上游把 JSON 字符串序列化成 "\"YoloV5\"" 这种带引号的形式
    if len(s) >= 2 and ((s[0] == '"' and s[-1] == '"') or (s[0] == "'" and s[-1] == "'")):
        s = s[1:-1].strip()
    return s


def _load_slave_backend_config(project_root: str) -> dict:
    cfg_path = (_SLAVE_BACKEND_CONFIG_PATH or "").strip()
    if not cfg_path:
        cfg_path = os.path.join(project_root, "config_files", "slave_backend.json")
    try:
        with open(cfg_path, "r", encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def _resolve_path(project_root: str, p: str) -> str:
    if not isinstance(p, str) or not p.strip():
        return ""
    if os.path.isabs(p):
        return p
    return os.path.abspath(os.path.join(project_root, p))


def _get_default_service(cfg: dict) -> str:
    v = cfg.get("default_service")
    return v if isinstance(v, str) and v.strip() else "Unknown"


def _get_service_entry(cfg: dict, service_name: str) -> dict:
    services = cfg.get("services")
    if isinstance(services, dict):
        v = services.get(service_name)
        if isinstance(v, dict):
            return v
    return {}


def _ensure_backend_via_agent(service_name: str, timeout_sec: int = 3) -> None:
    """
    由 agent 负责启动/守护后端（binary/container）。
    recv_server 仅负责接收任务并落盘；按需时调用 agent:POST /ensure_service。
    """
    conn = http.client.HTTPConnection("127.0.0.1", _agent_port(), timeout=timeout_sec)
    try:
        body = json.dumps({"service": service_name})
        conn.request("POST", "/ensure_service", body=body, headers={"Content-Type": "application/json"})
        resp = conn.getresponse()
        raw = resp.read()
        if resp.status >= 300:
            raise RuntimeError(f"agent ensure_service http {resp.status}: {raw[:200]!r}")
        try:
            payload = json.loads(raw.decode("utf-8")) if raw else {}
        except Exception:
            payload = {}
        if isinstance(payload, dict) and payload.get("status") not in (None, "success"):
            raise RuntimeError(f"agent ensure_service failed: {payload}")
    finally:
        conn.close()


def save_bytes_to_file(tmp_path: str, final_path: str, data: bytes) -> int:
    """在线程池中执行：写 tmp，再原子重命名到 final。返回写入字节数。"""
    ensure_dir(os.path.dirname(final_path))
    with open(tmp_path, "wb") as f:
        f.write(data)
    os.replace(tmp_path, final_path)
    return len(data)


def bump_and_maybe_log():
    """增加接收计数；每满 500 次记录一次时间戳到日志文件。"""
    global RECV_COUNT
    with RECV_LOCK:
        RECV_COUNT += 1
        cnt = RECV_COUNT
    if cnt % RECV_LOG_INTERVAL == 0:
        ensure_dir(LOG_DIR)
        ts = time.time()
        line = f"{int(ts)}\t{time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(ts))}\trecv_total={cnt}\n"
        with open(COUNT_LOG_FILE, "a", encoding="utf-8") as lf:
            lf.write(line)


# ===== /srv：接收图片并落盘（线程池执行写盘） =====
@app.post("/recv_task")
def srv():
    if not request.content_type or "multipart/form-data" not in request.content_type:
        return build_failed("expect multipart/form-data")

    # 1) 图片
    file_storage = request.files.get("pic_file") or (
        next(iter(request.files.values())) if request.files else None
    )
    if file_storage is None:
        return build_failed("missing image file part (pic_file)")

    # 2) JSON（优先 pic_info，其次 meta/json，最后遍历表单尝试解析）
    meta_raw = (
        request.form.get("pic_info")
        or request.form.get("meta")
        or request.form.get("json")
    )
    meta = None
    if meta_raw:
        try:
            meta = json.loads(meta_raw)
        except Exception as ex:
            return build_failed(f"bad meta json: {ex}")
    else:
        for v in request.form.values():
            try:
                cand = json.loads(v)
                if isinstance(cand, dict) and "ip" in cand and "file_name" in cand:
                    meta = cand
                    break
            except Exception:
                continue
        if meta is None:
            return build_failed("missing meta json (expect fields: ip, file_name)")

    # 3) 取 ip 与 file_name（task_type 暂不使用）
    src_ip = meta.get("ip")
    file_name = meta.get("file_name")
    tasktype = _normalize_tasktype(meta.get("tasktype", "Unknown"))
    if not isinstance(src_ip, str) or not src_ip.strip():
        return build_failed("meta.ip required")
    if not isinstance(file_name, str) or not file_name.strip():
        return build_failed("meta.file_name required")

    ext = os.path.splitext(file_name)[1].lower()
    if ext not in ALLOWED_EXTS:
        return build_failed("only jpg/jpeg/png/tif/tiff allowed")

    # 4) 读取文件到内存（落盘在线程池执行）
    data = file_storage.stream.read()

    # 5) service 选择：使用 tasktype 作为服务名；缺省时走 default_service
    cfg = _load_slave_backend_config(PROJECT_ROOT)
    service_name = tasktype if tasktype and tasktype != "Unknown" else _get_default_service(cfg)
    if not service_name or service_name == "Unknown":
        service_name = "Unknown"

    entry = _get_service_entry(cfg, service_name)
    if not entry:
        entry = {"backend": "local"}

    # 6) 确保后端已启动：统一由 agent 管理（binary/container）
    try:
        if entry.get("backend") in ("binary", "container"):
            _ensure_backend_via_agent(service_name)
    except Exception as e:
        return build_failed(f"start backend failed: {e}")

    # 7) 落盘到：<input_dir>/<client_ip>/<file_name>
    input_root = _resolve_path(PROJECT_ROOT, entry.get("input_dir", f"workspace/slave/data/input/{service_name}"))
    if not input_root:
        input_root = os.path.join(DATA_ROOT, service_name, "input")
    dir_path = os.path.join(input_root, src_ip)
    final_path = os.path.join(dir_path, os.path.basename(file_name))
    tmp_path = final_path + ".part"

    # 8) 写盘（线程池执行，避免阻塞 Flask worker）
    future = EXECUTOR.submit(save_bytes_to_file, tmp_path, final_path, data)
    try:
        size_bytes = future.result()  # 等待写盘完成后再返回
    finally:
        # 计数 + 每 500 记录一次时间戳
        bump_and_maybe_log()
        # 显式关闭文件流，释放底层资源
        file_storage.stream.close()
        # 主动释放data内存（关键步骤）
        data = None  # 清除引用，让GC可以回收

    return build_success(
        {
            "service": service_name,
            "saved_path": final_path,
            "from_ip": src_ip,
            "tasktype": tasktype,
            "size_bytes": size_bytes,
        }
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Slave recv_server: receive tasks and persist to service input dir")
    parser.add_argument(
        "--config",
        default=os.path.join(PROJECT_ROOT, "config_files", "slave_backend.json"),
        help="path to slave_backend.json (default: config_files/slave_backend.json)",
    )
    parser.add_argument(
        "--agent-port",
        type=int,
        default=8000,
        help="agent control port for POST /ensure_service (default: 8000)",
    )
    args = parser.parse_args()

    _SLAVE_BACKEND_CONFIG_PATH = os.path.abspath(args.config) if args.config else ""
    _AGENT_CONTROL_PORT = int(args.agent_port)

    print(f"[recv_server] listening on 0.0.0.0:{AGENT_PORT}")
    print("  Storage: use config_files/slave_backend.json services.<ServiceName>.{input_dir,output_dir,result_dir}")
    print(f"  Logs: {LOG_DIR}")
    
    # Flask 自身也开线程；我们的落盘再用 4 线程池，二者叠加可应对高并发 demo
    app.run(host="0.0.0.0", port=AGENT_PORT, threaded=True)
