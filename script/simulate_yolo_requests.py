#!/usr/bin/env python3
import argparse
import base64
import json
import os
import random
import time
import uuid

import grpc


def build_payload(filename: str, content_b64: str, tasktype: str, req_id: str, total_num: int) -> bytes:
    payload = {
        "filename": filename,
        "content_b64": content_b64,
        "tasktype": tasktype,
        "req_id": req_id,
        "total_num": total_num,
    }
    return json.dumps(payload).encode("utf-8")


def random_image_bytes(min_size: int, max_size: int) -> bytes:
    size = max(4, random.randint(min_size, max_size))
    return b"\xff\xd8" + os.urandom(size - 4) + b"\xff\xd9"


def pick_source_files(source_dir: str, count: int):
    files = []
    for name in os.listdir(source_dir):
        path = os.path.join(source_dir, name)
        if os.path.isfile(path):
            files.append(path)
    if not files:
        raise RuntimeError(f"no files found in {source_dir}")
    return [random.choice(files) for _ in range(count)]


def send_batch(host: str, port: int, tasktype: str, count: int, source_dir: str, min_size: int, max_size: int):
    req_id = f"req_{uuid.uuid4().hex}"
    channel = grpc.insecure_channel(f"{host}:{port}")
    method = "/ImageUpload/UploadImages"
    stub = channel.stream_unary(
        method,
        request_serializer=lambda b: b,
        response_deserializer=lambda b: b,
    )

    def gen():
        if source_dir:
            files = pick_source_files(source_dir, count)
        else:
            files = [None] * count
        for i in range(count):
            if files[i]:
                with open(files[i], "rb") as f:
                    content = f.read()
                filename = os.path.basename(files[i])
            else:
                content = random_image_bytes(min_size, max_size)
                filename = f"rand_{uuid.uuid4().hex}.jpg"
            content_b64 = base64.b64encode(content).decode("ascii")
            yield build_payload(filename, content_b64, tasktype, req_id, count)

    response = stub(gen())
    try:
        decoded = json.loads(response.decode("utf-8"))
    except Exception:
        decoded = {"raw": response.decode("utf-8", errors="replace")}
    print(f"sent batch req_id={req_id} count={count} response={decoded}")


def main():
    parser = argparse.ArgumentParser(description="Simulate YOLO requests via task_manager gRPC")
    parser.add_argument("--host", default="127.0.0.1", help="task_manager host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=9999, help="task_manager port (default: 9999)")
    parser.add_argument("--count", type=int, default=100, help="number of images to send")
    parser.add_argument("--tasktype", default="YoloV5", help="task type name")
    parser.add_argument("--source-dir", default="", help="optional source dir of images")
    parser.add_argument("--min-size", type=int, default=10240, help="min random image size (bytes)")
    parser.add_argument("--max-size", type=int, default=51200, help="max random image size (bytes)")
    parser.add_argument("--sleep", type=float, default=0.0, help="sleep seconds before sending")
    args = parser.parse_args()

    if args.sleep > 0:
        time.sleep(args.sleep)

    send_batch(
        host=args.host,
        port=args.port,
        tasktype=args.tasktype,
        count=args.count,
        source_dir=args.source_dir.strip(),
        min_size=args.min_size,
        max_size=args.max_size,
    )


if __name__ == "__main__":
    main()
