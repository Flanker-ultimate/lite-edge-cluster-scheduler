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
SUB_REQ_DIR = os.path.join(LOG_DIR, "sub_reqs")
TASK_MAP_FILE = os.path.join(LOG_DIR, "task_map.jsonl")

# 其他配置
AGENT_PORT = 20810
ALLOWED_EXTS = {".jpg", ".jpeg", ".png", ".tif", ".tiff"}

# 创建必要的目录
for path in [DATA_ROOT, LOG_DIR, SUB_REQ_DIR]:
    os.makedirs(path, exist_ok=True)

EXECUTOR = ThreadPoolExecutor(max_workers=4)

# 接收计数（进程级），与阈值
RECV_LOCK = threading.Lock()
RECV_COUNT = 0
RECV_LOG_INTERVAL = 500  # 每收到 500 张记录一次时间戳

app = Flask(__name__)

SUB_REQ_LOCK = threading.Lock()
SUB_REQS = {}
SUB_REQ_QUEUE_LOCK = threading.Lock()
SUB_REQ_QUEUE = {}
SUB_REQ_STATE = {}
SUB_REQ_WORKERS = {}
SUB_REQ_SEQ_LOCK = threading.Lock()
SUB_REQ_SEQ = {}

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

def _next_sub_req_seq(service_name: str) -> int:
    with SUB_REQ_SEQ_LOCK:
        cur = int(SUB_REQ_SEQ.get(service_name, 0))
        cur += 1
        SUB_REQ_SEQ[service_name] = cur
        return cur

def _sub_req_dir_name(seq: int, sub_req_id: str) -> str:
    return f"{seq:012d}__{_safe_name(sub_req_id)}"

def _safe_name(raw: str) -> str:
    out = []
    for ch in raw:
        if ch.isalnum() or ch in ("-", "_"):
            out.append(ch)
        else:
            out.append("_")
    return "".join(out) if out else "unknown"

def _sub_req_file_path(sub_req_id: str) -> str:
    return os.path.join(SUB_REQ_DIR, f"{_safe_name(sub_req_id)}.json")

def _write_sub_req_file(entry: dict) -> None:
    sub_req_id = entry.get("sub_req_id", "unknown")
    path = _sub_req_file_path(sub_req_id)
    tmp = path + ".part"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(entry, f, ensure_ascii=False)
    os.replace(tmp, path)

def _append_task_map(task_id: str, sub_req_id: str, req_id: str, tasktype: str, client_ip: str) -> None:
    line = json.dumps(
        {
            "task_id": task_id,
            "sub_req_id": sub_req_id,
            "req_id": req_id,
            "tasktype": tasktype,
            "client_ip": client_ip,
            "recv_time": time.time(),
        },
        ensure_ascii=False,
    )
    with open(TASK_MAP_FILE, "a", encoding="utf-8") as f:
        f.write(line + "\n")

def _resolve_service_name(tasktype: str, cfg: dict) -> str:
    ttype = _normalize_tasktype(tasktype)
    service_name = ttype if ttype and ttype != "Unknown" else _get_default_service(cfg)
    if not service_name:
        service_name = "Unknown"
    return service_name

def _promote_sub_req(state: dict) -> bool:
    staging_root = state.get("staging_root")
    ready_root = state.get("ready_root")
    if not staging_root or not ready_root:
        return False
    if state.get("promoted"):
        return True
    if os.path.isdir(ready_root):
        state["staging_root"] = ready_root
        state["promoted"] = True
        return True
    try:
        os.makedirs(os.path.dirname(ready_root), exist_ok=True)
        os.replace(staging_root, ready_root)
        state["staging_root"] = ready_root
        state["promoted"] = True
        return True
    except Exception:
        return False

def _sub_req_worker(service_name: str):
    while True:
        with SUB_REQ_QUEUE_LOCK:
            queue = SUB_REQ_QUEUE.get(service_name, [])
            sub_req_id = queue[0] if queue else ""
        if not sub_req_id:
            time.sleep(0.05)
            continue
        state = SUB_REQ_STATE.get(sub_req_id)
        if not state:
            with SUB_REQ_QUEUE_LOCK:
                queue = SUB_REQ_QUEUE.get(service_name, [])
                if queue and queue[0] == sub_req_id:
                    queue.pop(0)
            continue
        expected = int(state.get("expected") or 0)
        received = int(state.get("received") or 0)
        if received > 0:
            _promote_sub_req(state)
        if expected <= 0 or received < expected:
            time.sleep(0.05)
            continue
        with SUB_REQ_QUEUE_LOCK:
            queue = SUB_REQ_QUEUE.get(service_name, [])
            if queue and queue[0] == sub_req_id:
                queue.pop(0)
        SUB_REQ_STATE.pop(sub_req_id, None)

def _ensure_worker(service_name: str):
    if service_name in SUB_REQ_WORKERS:
        return
    t = threading.Thread(target=_sub_req_worker, args=(service_name,), daemon=True)
    SUB_REQ_WORKERS[service_name] = t
    t.start()

def _count_pending_files(root_dir: str) -> int:
    total = 0
    if not root_dir or not os.path.isdir(root_dir):
        return 0
    for dirpath, _, filenames in os.walk(root_dir):
        for name in filenames:
            if name.endswith(".part"):
                continue
            total += 1
    return total


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
@app.post("/recv_sub_req_meta")
def recv_sub_req_meta():
    payload = request.get_json(force=True, silent=True) or {}
    sub_req_id = payload.get("sub_req_id")
    req_id = payload.get("req_id")
    sub_req_count = payload.get("sub_req_count")
    if not isinstance(sub_req_id, str) or not sub_req_id.strip():
        return build_failed("sub_req_id required")
    if not isinstance(req_id, str) or not req_id.strip():
        return build_failed("req_id required")
    if not isinstance(sub_req_count, int) or sub_req_count <= 0:
        return build_failed("sub_req_count must be positive int")

    entry = {
        "req_id": req_id,
        "sub_req_id": sub_req_id,
        "sub_req_count": sub_req_count,
        "tasktype": payload.get("tasktype", "Unknown"),
        "dst_device_id": payload.get("dst_device_id", ""),
        "dst_device_ip": payload.get("dst_device_ip", ""),
        "enqueue_time_ms": payload.get("enqueue_time_ms", 0),
        "meta_time": time.time(),
        "start_time_ms": None,
        "expected_end_time_ms": 0,
        "queue_len_at_start": None,
        "client_ip": "",
        "service": "",
    }
    with SUB_REQ_LOCK:
        SUB_REQS[sub_req_id] = entry
    _write_sub_req_file(entry)

    cfg = _load_slave_backend_config(PROJECT_ROOT)
    service_name = _resolve_service_name(entry.get("tasktype", "Unknown"), cfg)
    entry["service"] = service_name
    svc_entry = _get_service_entry(cfg, service_name)
    input_root = _resolve_path(PROJECT_ROOT, svc_entry.get("input_dir", f"workspace/slave/data/input/{service_name}"))
    if not input_root:
        input_root = os.path.join(DATA_ROOT, service_name, "input")
    seq = _next_sub_req_seq(service_name)
    sub_dir_name = _sub_req_dir_name(seq, sub_req_id)
    service_dir = _safe_name(service_name)
    staging_root = os.path.join(input_root, "_sub_reqs_pending", service_dir, sub_dir_name)
    ready_root = os.path.join(input_root, "_sub_reqs_ready", service_dir, sub_dir_name)
    os.makedirs(staging_root, exist_ok=True)

    with SUB_REQ_QUEUE_LOCK:
        SUB_REQ_QUEUE.setdefault(service_name, []).append(sub_req_id)
    SUB_REQ_STATE[sub_req_id] = {
        "expected": sub_req_count,
        "received": 0,
        "service_name": service_name,
        "input_root": input_root,
        "staging_root": staging_root,
        "ready_root": ready_root,
        "seq": seq,
        "promoted": False,
    }
    _ensure_worker(service_name)

    return build_success({"sub_req_id": sub_req_id})


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
    sub_req_id = meta.get("sub_req_id")
    req_id = meta.get("req_id", "")
    target_root = input_root
    if isinstance(sub_req_id, str) and sub_req_id.strip():
        with SUB_REQ_LOCK:
            sub_req = SUB_REQS.get(sub_req_id)
            if sub_req and sub_req.get("start_time_ms") is None:
                sub_req["start_time_ms"] = int(time.time() * 1000)
                sub_req["queue_len_at_start"] = _count_pending_files(input_root)
                if not sub_req.get("client_ip"):
                    sub_req["client_ip"] = src_ip
                _write_sub_req_file(sub_req)
        with SUB_REQ_QUEUE_LOCK:
            state = SUB_REQ_STATE.get(sub_req_id)
            if state and state.get("staging_root"):
                target_root = state["staging_root"]
                state["received"] = int(state.get("received") or 0) + 1
        _append_task_map(file_name, sub_req_id, req_id, tasktype, src_ip)
    dir_path = os.path.join(target_root, src_ip)
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
