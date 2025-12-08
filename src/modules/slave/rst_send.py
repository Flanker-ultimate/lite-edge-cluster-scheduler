#!/usr/bin/env python3
"""
IP子目录图片监控与发送程序（严格模式）
功能：严格监控存在的input目录，优先处理最旧IP子目录，批量发送图片 发送路由为 ip子目录名:8888/
改进：
1. 修复空目录阻塞问题
2. 支持输入参数和默认值
使用方式：
# 使用默认目录 "/home/HwHiAiUser/co-compute-imgs/output/label"
# python3 rst_send.py

# 指定目录和间隔 --input-dir指定读取哪里的结果
# python3 rst_send.py --input-dir /path/to/dir --interval 10

# 使用短参数
python3 rst_send.py -i /path/to/dir -t 3
"""

import os
import time
import http.client
from urllib.parse import urlparse
import argparse

# 默认目录（当不指定参数时使用）
DEFAULT_INPUT_DIR = "/home/HwHiAiUser/co-compute-imgs/output/label"

class StrictIPSender:
    def __init__(self, input_dir: str, interval: int = 5):
        """
        Args:
            input_dir: 必须存在的input目录路径
            interval: 检查间隔(秒)
        """
        self.input_dir = os.path.abspath(input_dir)
        self.interval = interval
        
        # 验证目录是否存在
        if not os.path.exists(self.input_dir):
            raise FileNotFoundError(f"目录不存在: {self.input_dir}")
        if not os.path.isdir(self.input_dir):
            raise NotADirectoryError(f"路径不是目录: {self.input_dir}")
        
        print(f"严格模式监控目录: {self.input_dir}")
    
    def get_ip_dirs_sorted(self):
        """获取包含文件的IP子目录列表（旧优先）"""
        valid_ip_dirs = []
        try:
            for item in os.listdir(self.input_dir):
                item_path = os.path.join(self.input_dir, item)
                if os.path.isdir(item_path) and self._is_valid_ip(item):
                    # 检查目录是否包含文件
                    if any(os.path.isfile(os.path.join(item_path, f)) for f in os.listdir(item_path)):
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
        if ip_str.lower() == 'localhost':
            return True
            
        parts = ip_str.split('.')
        if len(parts) != 4:
            return False
        try:
            return all(0 <= int(part) <= 255 for part in parts)
        except ValueError:
            return False
    
    def send_files_to_ip(self, ip: str, ip_path: str):
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
            if self._send_single_file(file_path, ip):
                success_count += 1
                try:
                    os.remove(file_path)
                    print(f"🗑️  已删除: {filename}")
                except Exception as e:
                    print(f"❌ 删除失败 {filename}: {e}")
        
        print(f"✅ {ip} 处理完成: {success_count}/{len(files_to_send)} 成功")
    
    def _send_single_file(self, file_path: str, ip: str) -> bool:
        """发送单个文件到指定IP"""
        try:
            target_url = f"http://{ip}:8888/recv_rst"
            url_parts = urlparse(target_url)
            
            conn = http.client.HTTPConnection(
                host=url_parts.hostname,
                port=url_parts.port or 80,
                timeout=10
            )
            
            boundary = '----' + str(time.time()).encode().hex()
            headers = {'Content-Type': f'multipart/form-data; boundary={boundary}'}
            
            with open(file_path, 'rb') as f:
                file_content = f.read()
            
            filename = os.path.basename(file_path)
            body = (
                f"--{boundary}\r\n"
                f'Content-Disposition: form-data; name="file"; filename="{filename}"\r\n'
                f"Content-Type: application/octet-stream\r\n\r\n"
            ).encode() + file_content + f"\r\n--{boundary}--\r\n".encode()
            
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
        """处理下一个最旧的IP目录（只处理有文件的目录）"""
        ip_dirs = self.get_ip_dirs_sorted()
        if not ip_dirs:
            print(f"⏳ 未发现有效IP子目录，等待 {self.interval} 秒...")
            return False
        
        # 处理第一个最旧的目录
        ip, ip_path = ip_dirs[0]
        self.send_files_to_ip(ip, ip_path)
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
    parser = argparse.ArgumentParser(description='IP子目录图片监控与发送程序')
    parser.add_argument('--input-dir', '-i', 
                        default=DEFAULT_INPUT_DIR,
                        help=f'监控目录路径 (默认: {DEFAULT_INPUT_DIR})')
    parser.add_argument('--interval', '-t',
                        type=int,
                        default=5,
                        help='检查间隔时间(秒) (默认: 5)')
    return parser.parse_args()

if __name__ == "__main__":
    args = parse_args()
    
    try:
        sender = StrictIPSender(input_dir=args.input_dir, interval=args.interval)
        sender.run()
    except (FileNotFoundError, NotADirectoryError) as e:
        print(f"❌ 初始化失败: {e}")
        print("请确保：")
        print(f"1. 目录 {args.input_dir} 存在")
        print(f"2. 该路径是一个目录")
        exit(1)
