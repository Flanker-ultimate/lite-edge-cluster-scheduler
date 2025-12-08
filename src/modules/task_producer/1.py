import os
import sys
import argparse
import mimetypes
import uuid
import http.client
from urllib.parse import urlparse
from datetime import datetime

ip = "10.137.144.97"  # ！！写死的调度中心IP 使用时进行修改
dir_to_send = "/home/ubuntu/pics"  # 要发送的图片目录


def post_multipart(url: str, fields: dict, files: dict) -> tuple[int, bytes]:
    parsed = urlparse(url)
    if parsed.scheme not in ("http", "https"):
        raise ValueError("only http/https supported")

    boundary = f"----WebKitFormBoundary{uuid.uuid4().hex}"
    body_chunks = []
    crlf = "\r\n"

    for name, value in fields.items():
        body_chunks.append(f"--{boundary}{crlf}")
        body_chunks.append(f"Content-Disposition: form-data; name=\"{name}\"{crlf}{crlf}")
        body_chunks.append(f"{value}{crlf}")

    for name, filepath in files.items():
        filename = os.path.basename(filepath)
        guessed_type = mimetypes.guess_type(filename)[0] or "application/octet-stream"
        body_chunks.append(f"--{boundary}{crlf}")
        body_chunks.append(
            f"Content-Disposition: form-data; name=\"{name}\"; filename=\"{filename}\"{crlf}"
        )
        body_chunks.append(f"Content-Type: {guessed_type}{crlf}{crlf}")
        with open(filepath, "rb") as f:
            body_chunks.append(f.read())
        body_chunks.append(crlf)

    body_chunks.append(f"--{boundary}--{crlf}")

    body_bytes = b""
    for chunk in body_chunks:
        if isinstance(chunk, str):
            body_bytes += chunk.encode("utf-8")
        else:
            body_bytes += chunk

    if parsed.scheme == "https":
        conn = http.client.HTTPSConnection(parsed.hostname, parsed.port or 443, timeout=30)
    else:
        conn = http.client.HTTPConnection(parsed.hostname, parsed.port or 80, timeout=30)

    path = parsed.path or "/"
    if parsed.query:
        path += f"?{parsed.query}"

    headers = {"Content-Type": f"multipart/form-data; boundary={boundary}", "Content-Length": str(len(body_bytes))}
    conn.request("POST", path, body=body_bytes, headers=headers)
    resp = conn.getresponse()
    data = resp.read()
    conn.close()
    return resp.status, data


def list_image_files(directory: str) -> list:
    if not os.path.isdir(directory):
        return []
    files = []
    for name in os.listdir(directory):
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            continue
        # 不做任何类型判断，所有文件都加入发送列表
        files.append(path)
    def natural_key(p: str):
        base = os.path.splitext(os.path.basename(p))[0]
        try:
            return (0, int(base))
        except ValueError:
            return (1, base)
    return sorted(files, key=natural_key)


def main() -> None:
    parser = argparse.ArgumentParser(description="Send files in a directory via multipart/form-data")
    parser.add_argument("-n", "--max", type=int, default=None, help="限制发送的最大文件数量；不传为不限制")
    args = parser.parse_args()

    server_host = ip  
    server_port = 9000
    upload_path = "/upload"
    images_dir = dir_to_send
    # 解析相对路径
    if not os.path.isabs(images_dir):
        script_dir = os.path.dirname(os.path.abspath(__file__))
        images_dir = os.path.join(script_dir, images_dir)

    server_url = f"http://{server_host}:{server_port}{upload_path}"
    print(f"Sending images from: {images_dir} -> {server_url}")

    files = list_image_files(images_dir)
    if args.max is not None:
        limit = max(0, args.max)
        files = files[:limit]
    if not files:
        print("no images found")
        return
    # 打印开始发送第一张图片的时间戳（毫秒精度）
    first_ts = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
    print(f"first_send_timestamp: {first_ts}")

    for image_path in files:
        try:
            status, data = post_multipart(server_url, fields={}, files={"file": image_path})
            msg = data.decode('utf-8', errors='ignore')
            print(f"sent {os.path.basename(image_path)} -> status={status}, resp={msg}")
        except Exception as exc:
            print(f"error sending {image_path}: {exc}")


if __name__ == "__main__":
    main()


