
import socket
import sys
import re  # 用于正则表达式的匹配 提取IP地址和端口号
import os
import argparse
class FTP_CLIENT:
    def __init__(self, server_ip, server_port):
        self.server_ip = server_ip
        self.server_port = server_port
        self.control_socket = None
        self.data_socket = None
        self.pasv_ip = None
        self.pasv_port = None
        self.active_mode = False  # True: PORT模式, False: PASV模式
        self.pasv_mode = False  # True: PASV模式, False: PORT模式
        # 保存一个输出缓冲区队列
        self.output_buffer = []

    def connect(self):
        self.control_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.control_socket.connect((self.server_ip, self.server_port))
            self.receive_response()
        except Exception as e:
            sys.exit(1)

    def receive_response(self):
        response = ""
        while True:
            chunk = self.control_socket.recv(4096).decode('utf-8')
            response += chunk
            if not chunk or '\r\n' in chunk:
                break
        print(response.strip(), flush=True)
        return response.strip()

    def send_command(self, command):
        command += "\r\n"
        self.control_socket.sendall(command.encode('utf-8'))

    def close(self):
        self.control_socket.close()

    def login(self, username, password):
        """发送 USER 和 PASS 命令进行登录"""
        # 发送 USER 命令
        self.send_command(f"USER {username}")
        USER_response = self.receive_response()

        # 检查 USER 命令的响应
        if not USER_response.startswith("331"):
            sys.exit(1)

        # 发送 PASS 命令
        self.send_command(f"PASS {password}")
        PASS_response = self.receive_response()

        # 检查 PASS 命令的响应
        if not PASS_response.startswith("230"):
            sys.exit(1)
    
    def enter_port_mode(self, user_input):
        """进入 PORT 模式（主动模式）"""
        pattern = r"PORT (\d+),(\d+),(\d+),(\d+),(\d+),(\d+)"
        match = re.match(pattern, user_input, re.IGNORECASE)
        if not match:
            print("输入的 PORT 命令格式不正确")
            return False
        ip_parts = match.groups()[:4]
        ip_address = ".".join(ip_parts)
        p1, p2 = int(match.group(5)), int(match.group(6))
        port = p1 * 256 + p2
        # 先开始监听 在发送链接命令
        try:
            self.data_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.data_socket.bind((ip_address, port))
            self.data_socket.listen(1)
            self.active_mode = True
            self.pasv_mode = False
        except Exception as e:
            return False
        # 直接将用户输入的 `PORT` 命令发送给服务器
        self.send_command(user_input)
        # print(f"[INFO] PORT 命令已发送: {user_input}")
        response = self.receive_response()
        if not response.startswith("200"):
            print(f"[ERROR] PORT 命令失败: {response}")
            return False
        return True

    def data_connect(self, timeout=5):
        if self.active_mode:
            self.data_socket.settimeout(timeout)
            try:
                connection, addr = self.data_socket.accept()
                self.data_socket.settimeout(None)
                return connection
            except socket.timeout:
                return None

        elif self.pasv_mode:
            try:
                data_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                data_socket.settimeout(timeout)
                data_socket.connect((self.pasv_ip, self.pasv_port))
                data_socket.settimeout(None)
                return data_socket
            except socket.timeout:
                return None
            except Exception as e:
                return None

        else:
            return None

    def list_files(self, directory=None):
        """发送 LIST 命令并接收服务器返回的文件列表（自动进入 PASV 模式）"""
        if not self.pasv_mode:
            self.enter_pasv_mode()

        if directory:
            self.send_command(f"LIST {directory}")
        else:
            self.send_command("LIST")

        response = self.receive_response()
        if not response.startswith("150"):
            return

        data_connection = self.data_connect()
        if not data_connection:
            return

        data = b""
        while True:
            chunk = data_connection.recv(4096)
            if not chunk:
                break
            data += chunk

        data_connection.close()
        print(data.decode('utf-8').strip())

        self.receive_response()
        self.active_mode = False
        self.pasv_mode = False

    def enter_pasv_mode(self):
        """进入 PASV 模式（被动模式）"""
        self.send_command("PASV")
        response = self.receive_response()
        if not response.startswith("227"):
            return False

        pattern = r"227 Entering passive mode \((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)"
        match = re.search(pattern, response)
        if not match:
            # print("[ERROR] 无法解析 PASV 模式响应")
            return False

        ip_parts = match.groups()[:4]
        p1, p2 = int(match.group(5)), int(match.group(6))
        self.pasv_ip = ".".join(ip_parts)
        self.pasv_port = p1 * 256 + p2

        self.pasv_mode = True
        self.active_mode = False
        return True

    def retr(self, filename):
        if not self.ensure_data_connection():
            print("[ERROR] 无法建立有效的数据连接")
            return
        local_filename = filename.split("/")[-1]
        file_offset = 0
        if os.path.exists(local_filename):
            file_offset = os.path.getsize(local_filename)

        if file_offset > 0:
            self.send_command(f"REST {file_offset}")
            rest_response = self.receive_response()
            if not rest_response.startswith("350"):
                return

        # self.send_command(f"SIZE {filename}")
        # size_response = self.receive_response()
        # if not size_response.startswith("213"):
        #     return
        # print(f"[INFO] 开始下载文件: {filename}")
        self.send_command(f"RETR {filename}")
        response = self.receive_response() 
        if not (response.startswith("150") or response.startswith("125")):
            print(f"[ERROR] RETR 命令失败: {response}")
            return

        # total_size = int(size_response.split()[1])
        data_socket = self.data_connect()
        if data_socket is None:
            return

        bytes_received = file_offset
        with open(local_filename, "wb") as f:
            while True:
                chunk = data_socket.recv(4096)
                if not chunk:
                    break
                f.write(chunk)
                bytes_received += len(chunk)

        data_socket.close()
        self.receive_response()
        
        self.active_mode = False
        self.pasv_mode = False

    def stor(self, local_filename):
        if not self.ensure_data_connection():
            print("[ERROR] 无法建立有效的数据连接")
            return
        if not os.path.isfile(local_filename):
            return

        file_size = os.path.getsize(local_filename)
        self.send_command(f"SIZE {local_filename}")
        size_response = self.receive_response()
        if not size_response.startswith("213"):
            file_offset = 0
        else:
            file_offset = int(size_response.split()[1])

        if file_offset > 0 and file_offset < file_size:
            self.send_command(f"REST {file_offset}")
            rest_response = self.receive_response()
            if not rest_response.startswith("350"):
                return

        filename = os.path.basename(local_filename)
        self.send_command(f"STOR {filename}")
        response = self.receive_response()
        if not response.startswith("150") or not response.startswith("125"):
            print(f"[ERROR] STOR 命令失败: {response}")
            return

        data_socket = self.data_connect()
        if data_socket is None:
            print("[ERROR] 无法建立数据连接")
            return

        bytes_sent = file_offset
        with open(local_filename, "rb") as f:
            f.seek(file_offset)
            while True:
                chunk = f.read(4096)
                if not chunk:
                    break
                data_socket.sendall(chunk)

        print(f"[INFO] 文件上传完成: {local_filename}")
        data_socket.close()
        self.receive_response()
        self.active_mode = False
        self.pasv_mode = False

    def ensure_data_connection(self):
        if self.active_mode:
            if not self.data_socket or self.data_socket.fileno() == -1:
                return False
        elif self.pasv_mode:
            if not self.pasv_ip or not self.pasv_port:
                self.enter_pasv_mode()
        else:
            return False
        return True
    def resume_retr(self, filename):
        if not self.ensure_data_connection():
            print("[ERROR] 无法建立有效的数据连接")
            return
        local_filename = filename.split("/")[-1]
        file_offset = 0
        if os.path.exists(local_filename):
            file_offset = os.path.getsize(local_filename)

        if file_offset > 0:
            self.send_command(f"REST {file_offset}")
            rest_response = self.receive_response()
            if not rest_response.startswith("350"):
                return

        # 请求文件大小
        self.send_command(f"SIZE {filename}")
        size_response = self.receive_response()
        if not size_response.startswith("213"):
            print("[ERROR] 获取文件大小失败")
            return
        total_size = int(size_response.split()[1])

        # 发送 RETR 命令
        self.send_command(f"RETR {filename}")
        response = self.receive_response()
        if not (response.startswith("150") or response.startswith("125")):
            print(f"[ERROR] RETR 命令失败: {response}")
            return

        data_socket = self.data_connect()
        if data_socket is None:
            return

        # 显示下载进度
        bytes_received = file_offset
        with open(local_filename, "ab") as f:  # 以追加模式打开文件
            while True:
                chunk = data_socket.recv(4096)
                if not chunk:
                    break
                f.write(chunk)
                bytes_received += len(chunk)
                # 计算下载百分比并显示
                progress = (bytes_received / total_size) * 100
                print(f"\r[INFO] 下载进度: {progress:.2f}% ({bytes_received}/{total_size} bytes)", end='')

        print(f"\n[INFO] 文件下载完成: {local_filename}")
        data_socket.close()
        self.receive_response()
        
        self.active_mode = False
        self.pasv_mode = False
    def resume_stor(self, local_filename):
        if not self.ensure_data_connection():
            print("[ERROR] 无法建立有效的数据连接")
            return
        if not os.path.isfile(local_filename):
            print("[ERROR] 本地文件不存在")
            return

        file_size = os.path.getsize(local_filename)
        
        # 请求文件大小
        self.send_command(f"SIZE {os.path.basename(local_filename)}")
        size_response = self.receive_response()
        if not size_response.startswith("213"):
            file_offset = 0
        else:
            file_offset = int(size_response.split()[1])

        if file_offset > 0 and file_offset < file_size:
            self.send_command(f"REST {file_offset}")
            rest_response = self.receive_response()
            if not rest_response.startswith("350"):
                return

        # 发送 STOR 命令
        filename = os.path.basename(local_filename)
        self.send_command(f"STOR {filename}")
        response = self.receive_response()
        if not (response.startswith("150") or response.startswith("125")):
            print(f"[ERROR] STOR 命令失败: {response}")
            return

        data_socket = self.data_connect()
        if data_socket is None:
            print("[ERROR] 无法建立数据连接")
            return

        # 显示上传进度
        bytes_sent = file_offset
        with open(local_filename, "rb") as f:
            f.seek(file_offset)
            while True:
                chunk = f.read(4096)
                if not chunk:
                    break
                data_socket.sendall(chunk)
                bytes_sent += len(chunk)
                # 计算上传百分比并显示
                progress = (bytes_sent / file_size) * 100
                print(f"\r[INFO] 上传进度: {progress:.2f}% ({bytes_sent}/{file_size} bytes)", end='')

        print(f"\n[INFO] 文件上传完成: {local_filename}")
        data_socket.close()
        self.receive_response()
        
        self.active_mode = False
        self.pasv_mode = False
def parse_arguments():
    # 创建 ArgumentParser 对象
    parser = argparse.ArgumentParser(description="FTP Client")

    # 添加 -ip 和 -port 参数，带默认值
    parser.add_argument('-ip', type=str, default="127.0.0.1")
    parser.add_argument('-port', type=int, default=21)

    # 解析命令行参数
    args = parser.parse_args()

    return args.ip, args.port

def main():
    # 从命令行参数中获取服务器 IP 和端口（可以根据你的需要调整）
    server_ip, server_port = parse_arguments()

    # 创建 FTP 客户端实例
    client = FTP_CLIENT(server_ip, server_port)

    # 连接到 FTP 服务器
    client.connect()

    # 自动登录，使用默认用户名和密码
    # client.login("anonymous", "anonymous@example.com")

    # 循环处理输入命令
    while True:
        try:
            # 从stdin接收命令
            user_input = sys.stdin.readline().strip()

            if not user_input:
                continue

            # 处理不同的命令
            if user_input.upper() == "QUIT":
                client.send_command("QUIT")
                client.receive_response()
                break
            if user_input.upper() == "ABOR":
                client.send_command("ABOR")
                client.receive_response()
                break
            elif user_input.upper() == "SYST" or user_input.upper() == "TYPE I":
                client.send_command(user_input)
                client.receive_response()

            elif user_input.upper().startswith("PORT"):
                client.enter_port_mode(user_input.upper())

            elif user_input.upper() == "PASV":
                client.enter_pasv_mode()

            elif user_input.upper().startswith("LIST"):
                parts = user_input.split(maxsplit=1)
                directory = parts[1].strip() if len(parts) > 1 else None
                client.list_files(directory)

            elif user_input.upper().startswith("RETR"):
                parts = user_input.split(maxsplit=2)  # 允许处理多部分命令
                filename = parts[1].strip() if len(parts) > 1 else None
                if len(parts) > 2 and parts[2].strip().lower() == "-resume":
                    client.resume_retr(filename)  # 调用断点续传下载功能
                else:
                    client.retr(filename)  # 调用正常的下载功能

            elif user_input.upper().startswith("STOR"):
                parts = user_input.split(maxsplit=2)  # 允许处理多部分命令
                local_filename = parts[1].strip() if len(parts) > 1 else None
                if len(parts) > 2 and parts[2].strip().lower() == "-resume":
                    client.resume_stor(local_filename)  # 调用断点续传上传功能
                else:
                    client.stor(local_filename)  # 调用正常的上传功能

            else:
                client.send_command(user_input)
                client.receive_response()

        except EOFError:
            break  # 如果输入结束，则退出循环

    # 关闭客户端连接
    client.close()

if __name__ == "__main__":
    main()
