#!/usr/bin/env python3
"""
文件接收HTTP服务（支持参数配置版）
功能：接收文件并保持原始文件名保存到本地，支持自定义存储路径和端口
使用：
# 使用默认配置 默认保存到./received_files 默认端口8888
python3 rst_recv.py

# 指定端口和目录
python3 rst_recv.py --port 8888 --dir /tmp/received_files

# 使用短参数
python3 rst_recv.py -p 8080 -d ./custom_dir
"""

from http.server import BaseHTTPRequestHandler, HTTPServer
import os
import cgi
import argparse
from datetime import datetime
from threading import Lock  # 用于保证计数器线程安全

class FileReceiverHandler(BaseHTTPRequestHandler):
    # 类级变量：计数器和锁（确保多线程安全）
    file_count = 0
    count_lock = Lock()

    def __init__(self, storage_dir, *args, **kwargs):
        self.storage_dir = storage_dir
        os.makedirs(self.storage_dir, exist_ok=True)
        super().__init__(*args, **kwargs)
    
    def do_POST(self):
        if self.path == '/recv_rst':
            self.handle_file_upload()
        else:
            self.send_response(404)
            self.end_headers()
            self.wfile.write(b'404 Not Found')
    
    def handle_file_upload(self):
        try:
            content_type = self.headers.get('content-type')
            if not content_type or 'multipart/form-data' not in content_type:
                self.send_error(400, "需要multipart/form-data")
                return
                
            form = cgi.FieldStorage(
                fp=self.rfile,
                headers=self.headers,
                environ={'REQUEST_METHOD': 'POST'}
            )

            service = ""
            try:
                service = form.getfirst("service", "") or ""
            except Exception:
                service = ""
            service = service.strip()
            if "/" in service or "\\" in service:
                service = os.path.basename(service)
            if service in (".", ".."):
                service = ""
            
            if 'file' not in form:
                self.send_error(400, "需要文件字段'file'")
                return
                
            file_item = form['file']
            if not file_item.file:
                self.send_error(400, "无效的文件上传")
                return
                
            # 保持原始文件名
            filename = os.path.basename(file_item.filename)
            save_dir = self.storage_dir
            if service:
                save_dir = os.path.join(self.storage_dir, service)
            os.makedirs(save_dir, exist_ok=True)
            save_path = os.path.join(save_dir, filename)
            
            # 如果文件已存在则覆盖
            with open(save_path, 'wb') as f:
                file_item.file.seek(0)
                f.write(file_item.file.read())
            
            # 打印接收时间戳（毫秒精度）
            recv_ts = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
            print(f"recv_timestamp: {recv_ts}")

            print(f"收到文件保存为: {save_path}")
            
            # 计数器自增并检查是否达到500的倍数
            with self.count_lock:  # 加锁保证多线程安全
                self.file_count += 1
                if self.file_count % 500 == 0:
                    # 每接收500张图片打印一次时间戳
                    print(f"====== 已接收{self.file_count}张图片，当前时间：{recv_ts} ======")
            
            self.send_response(200)
            self.send_header('Content-type', 'text/plain')
            self.end_headers()
            self.wfile.write(b'File received')
            
        except Exception as e:
            self.send_error(500, f"服务器错误: {str(e)}")
            print(f"处理错误: {e}")
            # 错误也打印时间戳，便于对齐排查
            err_ts = datetime.now().strftime('%Y-%m-%d %H:%M:%S.%f')[:-3]
            print(f"recv_timestamp: {err_ts}")

def run_server(port=8888, storage_dir="./received_files"):
    """启动HTTP文件接收服务
    
    Args:
        port: 监听端口 (默认8888)
        storage_dir: 文件存储目录 (默认./received_files)
    """
    # 创建自定义请求处理器类
    handler_class = lambda *args, **kwargs: FileReceiverHandler(storage_dir, *args, **kwargs)
    
    server_address = ('', port)
    httpd = HTTPServer(server_address, handler_class)
    print(f"文件接收服务启动:")
    print(f" - 监听端口: {port}")
    print(f" - 存储路径: {os.path.abspath(storage_dir)}")
    
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    httpd.server_close()
    print("服务已停止")

def parse_args():
    """解析命令行参数"""
    parser = argparse.ArgumentParser(description='文件接收HTTP服务')
    parser.add_argument('--port', '-p', 
                        type=int,
                        default=8888,
                        help='监听端口 (默认: 8888)')
    parser.add_argument('--dir', '-d',
                        default="./received_files",
                        help='文件存储目录 (默认: ./received_files)')
    return parser.parse_args()

if __name__ == "__main__":
    args = parse_args()
    
    # 确保目录存在
    os.makedirs(args.dir, exist_ok=True)
    
    print("启动配置:")
    print(f" - 端口: {args.port}")
    print(f" - 存储目录: {os.path.abspath(args.dir)}")
    
    run_server(port=args.port, storage_dir=args.dir)
