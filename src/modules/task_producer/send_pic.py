import os
import argparse
import base64
import json
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import List, Tuple

import grpc


ip = "10.134.74.155"  
grpc_port = 9999
dir_to_send = "/home/ubuntu/test1026"


def list_image_files(directory: str) -> List[str]:
    if not os.path.isdir(directory):
        return []
    files: List[str] = []
    for name in os.listdir(directory):
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            continue
        files.append(path)

    def natural_key(p: str):
        base = os.path.splitext(os.path.basename(p))[0]
        try:
            return (0, int(base))
        except ValueError:
            return (1, base)

    return sorted(files, key=natural_key)


def upload_one(stub: grpc.Channel, filename: str, tasktype: str = "yolo") -> Tuple[str, int, str]:
    try:
        with open(filename, "rb") as f:
            content_b64 = base64.b64encode(f.read()).decode("ascii")
        payload = {"filename": os.path.basename(filename), "content_b64": content_b64, "tasktype": tasktype}
        # generic unary call
        resp_bytes = stub.unary_unary("/ImageUpload/UploadImage")(json.dumps(payload).encode("utf-8"))
        resp = json.loads(resp_bytes.decode("utf-8"))
        saved_path = resp.get("saved_path", "")
        return os.path.basename(filename), 200, f"saved:{saved_path}"
    except Exception as exc:
        return os.path.basename(filename), -1, f"error: {exc}"


def main() -> None:
    parser = argparse.ArgumentParser(description="gRPC single-image concurrent uploader")
    parser.add_argument("-n", "--max", type=int, default=None, help="限制发送的最大文件数量；不传为不限制")
    parser.add_argument("-w", "--workers", type=int, default=8, help="并发线程数 (默认: 8)")
    parser.add_argument("-H", "--host", default=ip, help="gRPC server host")
    parser.add_argument("-P", "--port", type=int, default=grpc_port, help="gRPC server port")
    parser.add_argument("-D", "--dir", default=dir_to_send, help="要发送的图片目录 (默认: ./pics)")
    args = parser.parse_args()

    images_dir = args.dir
    if not os.path.isabs(images_dir):
        script_dir = os.path.dirname(os.path.abspath(__file__))
        images_dir = os.path.join(script_dir, images_dir)

    files = list_image_files(images_dir)
    if args.max is not None:
        limit = max(0, args.max)
        files = files[:limit]
    if not files:
        print("no images found")
        return

    target = f"{args.host}:{args.port}"
    server_url = f"grpc://{args.host}:{args.port}/ImageUpload/UploadImage"
    print(f"Sending images from: {images_dir} -> {server_url}")
    # 与原版保持一致：首张发送时间戳（毫秒）
    from datetime import datetime
    first_ts = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
    print(f"first_send_timestamp: {first_ts}")

    with grpc.insecure_channel(
        target,
        options=[
            ("grpc.max_send_message_length", 128 * 1024 * 1024),
            ("grpc.max_receive_message_length", 128 * 1024 * 1024),
        ],
    ) as channel:
        with ThreadPoolExecutor(max_workers=args.workers) as executor:
            futures_map = {executor.submit(upload_one, channel, p): p for p in files}
            for fut in as_completed(futures_map):
                name, status, msg = fut.result()
                print(f"sent {name} -> status={status}, resp={msg}")


if __name__ == "__main__":
    main()


