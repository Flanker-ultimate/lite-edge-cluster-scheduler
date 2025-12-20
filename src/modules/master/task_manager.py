import os
import re
import json
import base64
import threading
import http.client
from urllib.parse import urlparse
from concurrent import futures
from typing import Dict, Iterator

import grpc
import time


DEFAULT_TASK_TYPE = "yolo"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
DEFAULT_UPLOAD_ROOT = os.path.join(SCRIPT_DIR, "files", "pic")


def ensure_directory_exists(path: str) -> None:
    if not os.path.isdir(path):
        os.makedirs(path, exist_ok=True)


def load_existing_sequences(upload_root: str) -> Dict[str, int]:
    sequences: Dict[str, int] = {}
    if not os.path.isdir(upload_root):
        return sequences
    ip_dir_names = [name for name in os.listdir(upload_root) if os.path.isdir(os.path.join(upload_root, name))]
    name_pattern = re.compile(r"^(?P<ip>[0-9a-fA-F:\.]+)_(?P<seq>\d+)(?P<ext>\.[A-Za-z0-9]+)?$")
    for ip_dir in ip_dir_names:
        dir_path = os.path.join(upload_root, ip_dir)
        max_seq = 0
        try:
            for fname in os.listdir(dir_path):
                match = name_pattern.match(fname)
                if not match:
                    continue
                if match.group("ip") != ip_dir:
                    continue
                seq_val = int(match.group("seq"))
                if seq_val > max_seq:
                    max_seq = seq_val
        except FileNotFoundError:
            continue
        sequences[ip_dir] = max_seq
    return sequences


class ImageUploadService:
    def __init__(self, upload_root: str, strategy: str = "load") -> None:
        self.upload_root = upload_root
        ensure_directory_exists(self.upload_root)
        self._ip_to_next_sequence = load_existing_sequences(self.upload_root)
        self._lock = threading.Lock()
        self.strategy = strategy

    def next_sequence_for_ip(self, client_ip: str) -> int:
        with self._lock:
            current = self._ip_to_next_sequence.get(client_ip, 0) + 1
            self._ip_to_next_sequence[client_ip] = current
            return current

    def _extract_peer_ip(self, context: grpc.ServicerContext) -> str:
        peer = context.peer()  # e.g., 'ipv4:127.0.0.1:53012'
        if peer.startswith("ipv4:"):
            return peer.split(":")[1]
        if peer.startswith("ipv6:"):
            # ipv6:[addr]:port
            try:
                inside = peer.split(":", 1)[1]
                if inside.startswith("["):
                    return inside.split("]", 1)[0][1:]
            except Exception:
                pass
        return "unknown"

    def _save_and_forward(self, client_ip: str, filename_hint: str, content_b64: str, tasktype: str) -> Dict[str, str]:
        t0 = time.perf_counter()
        file_bytes = base64.b64decode(content_b64)
        _, ext = os.path.splitext(filename_hint)
        t_parsed = time.perf_counter()

        ip_dir = os.path.join(self.upload_root, client_ip)
        ensure_directory_exists(ip_dir)

        seq = self.next_sequence_for_ip(client_ip)
        filename = f"{client_ip}_{seq}{ext}"
        save_path = os.path.join(ip_dir, filename)
        with open(save_path, "wb") as f:
            f.write(file_bytes)
        t_saved = time.perf_counter()

        print(f"saved {save_path} ({len(file_bytes)} bytes) from {client_ip}")

        picture_info = self._build_picture_info(client_ip, ip_dir, filename, tasktype)
        t_forward_start = time.perf_counter()
        try:
            self._forward_picture_info(picture_info)
        except Exception:
            pass
        finally:
            t_forward_end = time.perf_counter()

        # timing 日志（毫秒）与 HTTP 版一致的字段顺序与命名
        parse_ms = int((t_parsed - t0) * 1000)
        save_ms = int((t_saved - t_parsed) * 1000)
        forward_ms = int((t_forward_end - t_forward_start) * 1000)
        total_ms = int((t_forward_end - t0) * 1000)
        print(
            f"timing ip={client_ip} file={filename} size={len(file_bytes)}B "
            f"parse_ms={parse_ms} save_ms={save_ms} forward_ms={forward_ms} total_ms={total_ms}"
        )
        return {"filename": filename, "saved_path": save_path}

    def _build_picture_info(self, client_ip: str, dir_path: str, filename: str, tasktype: str) -> Dict[str, str]:
        try:
            rel_dir = os.path.relpath(dir_path, self.upload_root)
        except Exception:
            rel_dir = dir_path
        rel_dir = rel_dir.replace("\\", "/")
        if not rel_dir.endswith("/"):
            rel_dir += "/"
        return {
            "ip": client_ip,
            "tasktype": tasktype or DEFAULT_TASK_TYPE,
            "filepath": rel_dir,
            "filename": filename,
        }

    def _forward_picture_info(self, picture_info: Dict[str, str]) -> None:
        # 直接使用策略参数，无需映射转换
        scheduler_url = f"http://127.0.0.1:6666/schedule?stargety={self.strategy}"
        parsed = urlparse(scheduler_url)
        if parsed.scheme == "https":
            conn = http.client.HTTPSConnection(parsed.hostname, parsed.port or 443, timeout=5)
        else:
            conn = http.client.HTTPConnection(parsed.hostname, parsed.port or 80, timeout=5)
        path = parsed.path or "/"
        if parsed.query:
            path += f"?{parsed.query}"
        body = json.dumps(picture_info).encode("utf-8")
        headers = {"Content-Type": "application/json", "Content-Length": str(len(body))}
        try:
            conn.request("POST", path, body=body, headers=headers)
            resp = conn.getresponse()
            _ = resp.read()
        finally:
            conn.close()

    # ---- gRPC handlers (generic, JSON over gRPC) ----
    def UploadImage(self, request_bytes: bytes, context: grpc.ServicerContext) -> bytes:
        try:
            payload = json.loads(request_bytes.decode("utf-8"))
            filename = payload.get("filename", "")
            content_b64 = payload.get("content_b64", "")
            tasktype = payload.get("tasktype", DEFAULT_TASK_TYPE)
            client_ip = self._extract_peer_ip(context)
            result = self._save_and_forward(client_ip, filename, content_b64, tasktype)
            return json.dumps(result).encode("utf-8")
        except Exception as exc:
            context.set_code(grpc.StatusCode.INVALID_ARGUMENT)
            context.set_details(str(exc))
            return b""

    def UploadImages(self, request_iterator: Iterator[bytes], context: grpc.ServicerContext) -> bytes:
        client_ip = self._extract_peer_ip(context)
        saved_count = 0
        for req in request_iterator:
            try:
                payload = json.loads(req.decode("utf-8"))
                filename = payload.get("filename", "")
                content_b64 = payload.get("content_b64", "")
                tasktype = payload.get("tasktype", DEFAULT_TASK_TYPE)
                _ = self._save_and_forward(client_ip, filename, content_b64, tasktype)
                saved_count += 1
            except Exception:
                # skip this one, continue streaming
                continue
        return json.dumps({"saved_count": saved_count}).encode("utf-8")


def serve(
    host: str = "0.0.0.0",
    port: int = 50051,
    strategy: str = "load",
    upload_path: str = None,
) -> None:
    # 优先使用命令行传入的 upload_path，如果没有则使用默认值
    if upload_path is None:
        upload_root = DEFAULT_UPLOAD_ROOT
    else:
        upload_root = upload_path
    
    service = ImageUploadService(upload_root=upload_root, strategy=strategy)

    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=16),
        options=[
            ("grpc.max_send_message_length", 128 * 1024 * 1024),
            ("grpc.max_receive_message_length", 128 * 1024 * 1024),
        ],
    )

    method_handlers = {
        "UploadImage": grpc.unary_unary_rpc_method_handler(
            service.UploadImage,
            request_deserializer=lambda b: b,
            response_serializer=lambda b: b,
        ),
        "UploadImages": grpc.stream_unary_rpc_method_handler(
            service.UploadImages,
            request_deserializer=lambda b: b,
            response_serializer=lambda b: b,
        ),
    }

    generic_handler = grpc.method_handlers_generic_handler("ImageUpload", method_handlers)
    server.add_generic_rpc_handlers((generic_handler,))

    server.add_insecure_port(f"{host}:{port}")
    print(f"gRPC ImageUpload server listening on {host}:{port}")
    print(f"Saving files to: {os.path.abspath(upload_root)}")
    print(f"Forwarding strategy: {strategy}")
    server.start()
    try:
        server.wait_for_termination()
    except KeyboardInterrupt:
        print("shutting down...")
        server.stop(grace=None)


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description="gRPC ImageUpload server")
    parser.add_argument("-p", "--port", type=int, default=9999, help="gRPC listen port (default: 50051)")
    parser.add_argument("-s", "--strategy", choices=["load", "roundrobin"], default="load",
                        help="Scheduling strategy: load=load-based priority, roundrobin=round-robin scheduling (default: load)")
    # 添加新的上传路径参数
    parser.add_argument("-u", "--upload_path", type=str, default=None,
                        help=f"Custom upload directory path (default: {DEFAULT_UPLOAD_ROOT})")
    args = parser.parse_args()
    
    serve(
        port=args.port,
        strategy=args.strategy,
        upload_path=args.upload_path,
    )
