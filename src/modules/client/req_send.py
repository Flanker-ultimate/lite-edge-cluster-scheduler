import os
import argparse
import base64
import json
from concurrent.futures import ThreadPoolExecutor, as_completed
from typing import List, Tuple

import grpc


def project_root() -> str:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(script_dir, "../../.."))


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


def upload_one(stub: grpc.Channel, filename: str, tasktype: str = "YoloV5") -> Tuple[str, int, str]:
    try:
        with open(filename, "rb") as f:
            content_b64 = base64.b64encode(f.read()).decode("ascii")
        payload = {"filename": os.path.basename(filename), "content_b64": content_b64, "tasktype": tasktype}
        resp_bytes = stub.unary_unary("/ImageUpload/UploadImage")(json.dumps(payload).encode("utf-8"))
        resp = json.loads(resp_bytes.decode("utf-8"))
        saved_path = resp.get("saved_path", "")
        return os.path.basename(filename), 200, f"saved:{saved_path}"
    except Exception as exc:
        return os.path.basename(filename), -1, f"error: {exc}"


def upload_batch(stub: grpc.Channel, files: List[str], tasktype: str, req_id: str = "") -> Tuple[int, str]:
    total_num = len(files)

    def payload_iter():
        for idx, filename in enumerate(files):
            with open(filename, "rb") as f:
                content_b64 = base64.b64encode(f.read()).decode("ascii")
            payload = {
                "filename": os.path.basename(filename),
                "content_b64": content_b64,
                "tasktype": tasktype,
            }
            if req_id:
                payload["req_id"] = req_id
            if idx == 0:
                payload["total_num"] = total_num
            yield json.dumps(payload).encode("utf-8")

    try:
        resp_bytes = stub.stream_unary("/ImageUpload/UploadImages")(payload_iter())
        resp = json.loads(resp_bytes.decode("utf-8"))
        return resp.get("saved_count", 0), "ok"
    except Exception as exc:
        return -1, f"error: {exc}"


def main() -> None:
    parser = argparse.ArgumentParser(description="gRPC request sender (service-aware)")
    parser.add_argument("-n", "--max", type=int, default=None, help="limit max files to send")
    parser.add_argument("-w", "--workers", type=int, default=8, help="concurrency (default: 8)")
    parser.add_argument("-H", "--host", default="127.0.0.1", help="gRPC server host")
    parser.add_argument("-P", "--port", type=int, default=9999, help="gRPC server port")
    parser.add_argument(
        "-D",
        "--dir",
        default="",
        help="input directory (override); default uses workspace/client/data/<tasktype>/req",
    )
    parser.add_argument(
        "--root",
        default="workspace/client/data",
        help="client data root directory (default: workspace/client/data)",
    )
    parser.add_argument("--tasktype", default="YoloV5", help="service/task type (e.g. YoloV5, Bert, ...)")
    parser.add_argument("--batch", action="store_true", help="send all files as one gRPC batch")
    parser.add_argument("--req-id", default="", help="optional req_id for batch mode")
    args = parser.parse_args()

    root_dir = args.root
    if not os.path.isabs(root_dir):
        root_dir = os.path.join(project_root(), root_dir)

    images_dir = args.dir.strip()
    if not os.path.isabs(images_dir):
        if images_dir:
            images_dir = os.path.join(project_root(), images_dir)
        else:
            images_dir = os.path.join(root_dir, args.tasktype, "req")

    files = list_image_files(images_dir)
    if args.max is not None:
        limit = max(0, args.max)
        files = files[:limit]
    if not files:
        print(f"No images found in directory: {images_dir}")
        return

    target = f"{args.host}:{args.port}"
    server_url = f"grpc://{args.host}:{args.port}/ImageUpload/UploadImage"
    print(f"Sending images from: {images_dir} -> {server_url} (tasktype={args.tasktype})")

    with grpc.insecure_channel(
        target,
        options=[
            ("grpc.max_send_message_length", 128 * 1024 * 1024),
            ("grpc.max_receive_message_length", 128 * 1024 * 1024),
        ],
    ) as channel:
        if args.batch:
            saved_count, msg = upload_batch(channel, files, args.tasktype, req_id=args.req_id)
            print(f"batch sent {len(files)} files -> saved_count={saved_count}, resp={msg}")
        else:
            with ThreadPoolExecutor(max_workers=args.workers) as executor:
                futures_map = {executor.submit(upload_one, channel, p, args.tasktype): p for p in files}
                for fut in as_completed(futures_map):
                    name, status, msg = fut.result()
                    print(f"sent {name} -> status={status}, resp={msg}")


if __name__ == "__main__":
    main()
