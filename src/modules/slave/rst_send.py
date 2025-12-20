#!/usr/bin/env python3
"""
IP子目录图片监控与发送程序（严格模式）
功能：严格监控存在的input目录，优先处理最旧IP子目录，批量发送图片
改进：
1. 修复空目录阻塞问题
2. 支持输入参数和默认值
3. 支持自定义目标端口
使用方式：
# 使用默认目录和端口
python3 rst_send.py

# 指定目录和间隔
python3 rst_send.py --input-dir /path/to/dir --interval 10

# 使用自定义端口
python3 rst_send.py --input-dir /path/to/dir --target-port 9999

# 使用短参数
python3 rst_send.py -i /path/to/dir -t 3 -p 9999
"""

import argparse
import http.client
import json
import os
import time
from urllib.parse import urlparse
from typing import List, Tuple

CURRENT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(CURRENT_DIR, "../../../"))

# 默认扫描根目录（多 service 模式下主要由 --config 决定 result_dir）
DEFAULT_INPUT_DIR = os.path.join(PROJECT_ROOT, "workspace", "slave", "data")

# 默认配置（仅默认值；建议由命令行显式传入）
DEFAULT_GATEWAY_HOST = "127.0.0.1"
DEFAULT_GATEWAY_PORT = 6666
DEFAULT_DEVICE_ID = "unknown"
DEFAULT_SLAVE_BACKEND_CONFIG = os.path.join(PROJECT_ROOT, "config_files", "slave_backend.json")


class StrictIPSender:
    def __init__(
        self,
        input_dir: str,
        interval: int = 5,
        target_port: int = 8888,
        gateway_host: str = DEFAULT_GATEWAY_HOST,
        gateway_port: int = DEFAULT_GATEWAY_PORT,
        device_id: str = DEFAULT_DEVICE_ID,
        config_path: str = DEFAULT_SLAVE_BACKEND_CONFIG,
        service: str = "",
    ):
        """
        Args:
            input_dir: 必须存在的input目录路径
            interval: 检查间隔(秒)
            target_port: 目标端口，用于发送文件
        """
        self.input_dir = os.path.abspath(input_dir)
        self.interval = interval
        self.target_port = target_port
        self.gateway_host = gateway_host
        self.gateway_port = int(gateway_port)
        self.device_id = device_id
        self.config_path = os.path.abspath(config_path) if config_path else ""
        self.service = service.strip()
        self.service_dirs = self._load_service_result_dirs()

        # 兼容旧模式：未配置服务目录时，仍按 input_dir 扫描 <ip>/...
        if not self.service_dirs:
            if not os.path.exists(self.input_dir):
                raise FileNotFoundError(f"directory not found: {self.input_dir}")
            if not os.path.isdir(self.input_dir):
                raise NotADirectoryError(f"not a directory: {self.input_dir}")
            print(f"[rst_send] single-dir mode: {self.input_dir}")
        else:
            for _, p in self.service_dirs:
                os.makedirs(p, exist_ok=True)
            print("[rst_send] multi-service mode:")
            for svc, p in self.service_dirs:
                print(f"  - {svc}: {p}")

    def _load_service_result_dirs(self) -> List[Tuple[str, str]]:
        if not self.config_path or not os.path.isfile(self.config_path):
            return []
        try:
            with open(self.config_path, "r", encoding="utf-8") as f:
                cfg = json.load(f)
        except Exception:
            return []

        services = cfg.get("services") if isinstance(cfg, dict) else None
        if not isinstance(services, dict):
            return []

        out: List[Tuple[str, str]] = []
        for name, entry in services.items():
            if not isinstance(name, str) or not isinstance(entry, dict):
                continue
            if self.service and name != self.service:
                continue
            result_dir = entry.get("result_dir")
            if not isinstance(result_dir, str) or not result_dir.strip():
                continue
            if not os.path.isabs(result_dir):
                result_dir = os.path.abspath(os.path.join(PROJECT_ROOT, result_dir))
            out.append((name, result_dir))
        return out

    def notify_gateway_task_completed(self, task_id: str, client_ip: str, service: str, status: str = "success") -> bool:
        """通知gateway该任务已完成（或失败）。"""
        try:
            conn = http.client.HTTPConnection(self.gateway_host, self.gateway_port, timeout=5)
            payload = json.dumps(
                {
                    "task_id": task_id,
                    "device_id": self.device_id,
                    "client_ip": client_ip,
                    "service": service,
                    "status": status,
                }
            ).encode("utf-8")
            headers = {"Content-Type": "application/json", "Content-Length": str(len(payload))}
            conn.request("POST", "/task_completed", body=payload, headers=headers)
            resp = conn.getresponse()
            _ = resp.read()
            return resp.status == 200
        except Exception as e:
            print(f"gateway notify error: {e}")
            return False
        finally:
            try:
                conn.close()
            except Exception:
                pass

    def get_ip_dirs_sorted(self, root_dir: str):
        """获取包含文件的IP子目录列表（旧优先）"""
        valid_ip_dirs = []
        try:
            for item in os.listdir(root_dir):
                item_path = os.path.join(root_dir, item)
                if os.path.isdir(item_path) and self._is_valid_ip(item):
                    # 检查目录是否包含文件
                    if any(
                        os.path.isfile(os.path.join(item_path, f))
                        for f in os.listdir(item_path)
                    ):
                        mtime = os.path.getmtime(item_path)
                        valid_ip_dirs.append((mtime, item, item_path))

            # 按修改时间排序（旧优先）
            valid_ip_dirs.sort(key=lambda x: x[0])
            return [(ip, path) for _, ip, path in valid_ip_dirs]
        except Exception as e:
            print(f"扫描目录出错: {e}")
            return []

    def _is_valid_ip(self, ip_str):
        """严格验证IP地址格式，同时允许localhost"""
        if ip_str.lower() == "localhost":
            return True

        parts = ip_str.split(".")
        if len(parts) != 4:
            return False
        try:
            return all(0 <= int(part) <= 255 for part in parts)
        except ValueError:
            return False

    def send_files_to_ip(self, service: str, ip: str, ip_path: str):
        """批量发送目录下所有文件到对应IP"""
        files_to_send = []

        # 收集所有文件（不限制格式）
        for filename in os.listdir(ip_path):
            file_path = os.path.join(ip_path, filename)
            if os.path.isfile(file_path):
                files_to_send.append((filename, file_path))

        if not files_to_send:
            print(f"⚠️  {ip} 目录下没有文件")
            return

        print(f"📁 处理 {ip} 目录 (共 {len(files_to_send)} 个文件)")

        # 批量发送
        success_count = 0
        for filename, file_path in files_to_send:
            if self._send_single_file(file_path, ip, service):
                notified = self.notify_gateway_task_completed(task_id=filename, client_ip=ip, service=service, status="success")
                if not notified:
                    print(f"gateway notify failed, keep file for retry: {filename}")
                    continue
                success_count += 1
                try:
                    os.remove(file_path)
                    print(f"🗑️  已删除: {filename}")
                except Exception as e:
                    print(f"❌ 删除失败 {filename}: {e}")

        print(f"✅ {ip} 处理完成: {success_count}/{len(files_to_send)} 成功")

    def _send_single_file(self, file_path: str, ip: str, service: str) -> bool:
        """发送单个文件到指定IP"""
        try:
            target_url = f"http://{ip}:{self.target_port}/recv_rst"
            url_parts = urlparse(target_url)

            conn = http.client.HTTPConnection(
                host=url_parts.hostname, port=url_parts.port, timeout=10
            )

            boundary = "----" + str(time.time()).encode().hex()
            headers = {"Content-Type": f"multipart/form-data; boundary={boundary}"}

            with open(file_path, "rb") as f:
                file_content = f.read()

            filename = os.path.basename(file_path)
            body = b""
            body += f"--{boundary}\r\n".encode()
            body += f'Content-Disposition: form-data; name="service"\r\n\r\n{service}\r\n'.encode()
            body += f"--{boundary}\r\n".encode()
            body += (
                f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
                f"Content-Type: application/octet-stream\r\n\r\n"
            ).encode()
            body += file_content
            body += f"\r\n--{boundary}--\r\n".encode()

            conn.request("POST", url_parts.path, body, headers)
            response = conn.getresponse()
            response.read()  # 必须读取响应数据

            if response.status == 200:
                print(f"📤 发送成功: {filename} -> {ip}")
                return True
            else:
                print(f"❌ 发送失败到 {ip} [{response.status}]")
                return False

        except Exception as e:
            print(f"⚠️  发送到 {ip} 出错: {e}")
            return False

    def process_next_ip(self):
        """处理下一个最旧的IP目录（多服务模式会跨 service 选择最旧的目录）"""
        candidates: List[Tuple[float, str, str, str]] = []

        if self.service_dirs:
            for svc, root_dir in self.service_dirs:
                ip_dirs = self.get_ip_dirs_sorted(root_dir)
                if not ip_dirs:
                    continue
                ip, ip_path = ip_dirs[0]
                try:
                    mtime = os.path.getmtime(ip_path)
                except Exception:
                    mtime = time.time()
                candidates.append((mtime, svc, ip, ip_path))
        else:
            ip_dirs = self.get_ip_dirs_sorted(self.input_dir)
            if ip_dirs:
                ip, ip_path = ip_dirs[0]
                try:
                    mtime = os.path.getmtime(ip_path)
                except Exception:
                    mtime = time.time()
                candidates.append((mtime, "default", ip, ip_path))

        if not candidates:
            print(f"⏳ 未发现有效IP子目录，等待 {self.interval} 秒...")
            return False

        candidates.sort(key=lambda x: x[0])
        _, svc, ip, ip_path = candidates[0]
        self.send_files_to_ip(svc, ip, ip_path)
        return True

    def run(self):
        """启动严格监控模式"""
        print("启动严格模式监控...")
        print("=" * 50)

        try:
            while True:
                if not self.process_next_ip():
                    time.sleep(self.interval)
        except KeyboardInterrupt:
            print("\n监控已停止")
        except Exception as e:
            print(f"监控出错: {e}")


def parse_args():
    """解析命令行参数"""
    parser = argparse.ArgumentParser(description="IP子目录图片监控与发送程序")
    parser.add_argument(
        "--input-dir",
        "-i",
        default=DEFAULT_INPUT_DIR,
        help=f"监控目录路径 (默认: {DEFAULT_INPUT_DIR})",
    )
    parser.add_argument(
        "--config",
        default=DEFAULT_SLAVE_BACKEND_CONFIG,
        help=f"slave backend config (default: {DEFAULT_SLAVE_BACKEND_CONFIG})",
    )
    parser.add_argument(
        "--service",
        default="",
        help="only send results for a specific service name (e.g. YoloV5)",
    )
    parser.add_argument(
        "--interval", "-t", type=int, default=5, help="检查间隔时间(秒) (默认: 5)"
    )
    parser.add_argument(
        "--target-port",
        "-p",
        type=int,
        default=8888,
        help="目标端口 (默认: 8888)",
    )
    parser.add_argument(
        "--gateway-host",
        default=DEFAULT_GATEWAY_HOST,
        help=f"gateway host (default: {DEFAULT_GATEWAY_HOST})",
    )
    parser.add_argument(
        "--gateway-port",
        type=int,
        default=DEFAULT_GATEWAY_PORT,
        help=f"gateway port (default: {DEFAULT_GATEWAY_PORT})",
    )
    parser.add_argument(
        "--device-id",
        default=DEFAULT_DEVICE_ID,
        help=f"device id (default: {DEFAULT_DEVICE_ID})",
    )
    return parser.parse_args()


if __name__ == "__main__":
    args = parse_args()

    try:
        sender = StrictIPSender(
            input_dir=args.input_dir,
            interval=args.interval,
            target_port=args.target_port,
            gateway_host=args.gateway_host,
            gateway_port=args.gateway_port,
            device_id=args.device_id,
            config_path=args.config,
            service=args.service,
        )
        sender.run()
    except (FileNotFoundError, NotADirectoryError) as e:
        print(f"❌ 初始化失败: {e}")
        print("请确保：")
        print(f"1. 目录 {args.input_dir} 存在")
        print(f"2. 该路径是一个目录")
        exit(1)
