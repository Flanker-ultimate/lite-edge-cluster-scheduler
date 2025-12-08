#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import json
import shlex
import time
import threading
import subprocess
from concurrent.futures import ThreadPoolExecutor
from flask import Flask, request, jsonify
import atexit
import signal
# ===== 基本配置 =====
IMG_ROOT      = os.environ.get("IMG_ROOT", "/home/ubuntu/co-compute-imgs/input")
AGENT_PORT    = int(os.environ.get("AGENT_PORT", "20810"))
YOLO_CMD      = os.environ.get("YOLO_CMD",
        "python3 /home/ubuntu/edge-cluster-scheduler/src/modules/slave/yolov5-ascend/detect_yolov5_ascend.py \
        --input-dir /home/ubuntu/co-compute-imgs/input \
        --output-dir /home/ubuntu/co-compute-imgs/output \
        --output-format all")  # 为空则不启动 yolo
YOLO_LOG = os.environ.get("YOLO_LOG", "/home/ubuntu/co-compute-imgs/yolo.log")

# 计数日志目录（默认写到 IMG_ROOT），文件名固定 receive_stats.log
COUNT_LOG_DIR  = os.environ.get("COUNT_LOG_DIR", IMG_ROOT)
COUNT_LOG_FILE = os.path.join("/home/ubuntu/co-compute-imgs/receive_stats.log")

ALLOWED_EXTS = {".jpg", ".jpeg", ".png", ".tif", ".tiff"}

# 并行落盘线程池（4 线程）
EXECUTOR = ThreadPoolExecutor(max_workers=4)

# 接收计数（进程级），与阈值
RECV_LOCK = threading.Lock()
RECV_COUNT = 0
RECV_LOG_INTERVAL = 500  # 每收到 500 张记录一次时间戳

app = Flask(__name__)

# ===== 统一返回（失败也返回 200） =====
def build_success(result):
    return jsonify({"status": "success", "result": result}), 200

def build_failed(error):
    payload = {"error": error} if isinstance(error, str) else error
    return jsonify({"status": "failed", "result": payload}), 200

@app.errorhandler(Exception)
def on_exception(e):
    return build_failed(str(e))

# ===== 工具函数 =====
def ensure_dir(path: str):
    os.makedirs(path, exist_ok=True)

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
        ensure_dir(COUNT_LOG_DIR)
        ts = time.time()
        line = f"{int(ts)}\t{time.strftime('%Y-%m-%d %H:%M:%S', time.localtime(ts))}\trecv_total={cnt}\n"
        with open(COUNT_LOG_FILE, "a", encoding="utf-8") as lf:
            lf.write(line)

# ===== /srv：接收图片并落盘（线程池执行写盘） =====
# 协议（multipart/form-data）：
#   - 文件：pic_file
#   - JSON：pic_info（兼容 meta/json），包含 {"ip":"x.x.x.x","file_name":"name.ext","task_type":"..."}
@app.post("/recv_task")
def srv():
    if not request.content_type or "multipart/form-data" not in request.content_type:
        return build_failed("expect multipart/form-data")

    # 1) 图片
    file_storage = request.files.get("pic_file") or (next(iter(request.files.values())) if request.files else None)
    if file_storage is None:
        return build_failed("missing image file part (pic_file)")

    # 2) JSON（优先 pic_info，其次 meta/json，最后遍历表单尝试解析）
    meta_raw = request.form.get("pic_info") or request.form.get("meta") or request.form.get("json")
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
    if not isinstance(src_ip, str) or not src_ip.strip():
        return build_failed("meta.ip required")
    if not isinstance(file_name, str) or not file_name.strip():
        return build_failed("meta.file_name required")

    ext = os.path.splitext(file_name)[1].lower()
    if ext not in ALLOWED_EXTS:
        return build_failed("only jpg/jpeg/png/tif/tiff allowed")

    # 4) 目标路径
    dir_path   = os.path.join(IMG_ROOT, src_ip)
    final_path = os.path.join(dir_path, os.path.basename(file_name))
    tmp_path   = final_path + ".part"

    # 5) 读取文件到内存（简化 demo），把写盘工作交给线程池并等待完成
    data = file_storage.stream.read()
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

    return build_success({
        "saved_path": final_path,
        "from_ip": src_ip,
        "size_bytes": size_bytes
    })

# ===== 启动 yolo.py （日志重定向，行缓冲） =====
_yolo_proc = None

def start_yolo():
    global _yolo_proc
    if not YOLO_CMD.strip():
        print("[YOLO] YOLO_CMD is empty, skip start (demo)")
        return False
    try:
        log_dir = os.path.dirname(YOLO_LOG) or "."
        ensure_dir(log_dir)

        # 直接用'w'模式打开，会自动创建新文件（若存在则清空内容）
        logf = open(YOLO_LOG, "w", buffering=1, encoding="utf-8")
        env = os.environ.copy()
        env.setdefault("PYTHONUNBUFFERED", "1")
        cmd = shlex.split(YOLO_CMD)
        _yolo_proc = subprocess.Popen(
            cmd,
            stdout=logf,
            stderr=subprocess.STDOUT,
            start_new_session=True,
            close_fds=True,
            env=env
        )
        print(f"[YOLO] started: pid={_yolo_proc.pid}, cmd={' '.join(cmd)}, log={YOLO_LOG}")
        return True
    except Exception as e:
        print(f"[YOLO] start failed: {e}")
        return False

def stop_yolo():
    global _yolo_proc
    if _yolo_proc and _yolo_proc.poll() is None:  # 仍在运行
        print(f"[YOLO] stopping pid={_yolo_proc.pid}")
        try:
            os.killpg(os.getpgid(_yolo_proc.pid), signal.SIGTERM)
        except Exception as e:
            print(f"[YOLO] stop failed: {e}")

# ===== 入口 =====
if __name__ == "__main__":

    atexit.register(stop_yolo)

    start_yolo()
    print(f"[Agent] listening on 0.0.0.0:{AGENT_PORT}, IMG_ROOT={IMG_ROOT}")
    # Flask 自身也开线程；我们的落盘再用 4 线程池，二者叠加可应对高并发 demo
    app.run(host="0.0.0.0", port=AGENT_PORT, threaded=True)