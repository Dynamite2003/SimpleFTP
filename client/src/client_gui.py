
import socket
import sys
import re  # 用于正则表达式的匹配 提取IP地址和端口号
import os
import argparse
import tkinter as tk
from tkinter import filedialog, messagebox, ttk, simpledialog
import threading
from ttkthemes import ThemedTk
import time


class FTP_CLIENT:
    def __init__(self, server_ip, server_port, download_callback=None, upload_callback=None,
                 client_output_callback=None, server_output_callback=None):
        self.server_ip = server_ip
        self.server_port = server_port
        self.control_socket = None
        self.data_socket = None
        self.pasv_ip = None
        self.pasv_port = None
        self.active_mode = False
        self.pasv_mode = False
        self.output_buffer = []
        self.download_callback = download_callback  # 用于更新下载进度
        self.upload_callback = upload_callback      # 用于更新上传进度
        self.client_output_callback = client_output_callback    # 客户端输出回调
        self.server_output_callback = server_output_callback    # 服务器输出回调

    def connect(self):
        self.control_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            self.client_output_callback("Connecting to server...\n")
            self.control_socket.connect((self.server_ip, self.server_port))
            response = self.receive_response()
            self.server_output_callback(response + "\n")
            self.client_output_callback("Connected to server.\n")
        except Exception as e:
            self.client_output_callback(f"Connection failed: {e}\n")
            sys.exit(1)

    def receive_response(self):
        response = ""
        while True:
            chunk = self.control_socket.recv(4096).decode('utf-8')
            response += chunk
            if '\r\n' in chunk:
                break
        self.server_output_callback(response.strip() + "\n")
        return response.strip()

    def send_command(self, command):
        self.client_output_callback(f">>> {command}\n")
        command += "\r\n"
        self.control_socket.sendall(command.encode('utf-8'))

    def close(self):
        if self.control_socket:
            self.send_command("QUIT")
            try:
                response = self.receive_response()
                self.server_output_callback(response + "\n")
            except Exception as e:
                self.client_output_callback(f"Error receiving QUIT response: {e}\n")
            self.control_socket.close()
            self.control_socket = None
            self.client_output_callback("Disconnected from server.\n")
        
        # 关闭任何活动的数据连接
        if self.data_socket:
            try:
                self.data_socket.close()
                self.client_output_callback("Data connection closed.\n")
            except Exception as e:
                self.client_output_callback(f"Error closing data connection: {e}\n")
            self.data_socket = None

    def login(self, username, password):
        """发送 USER 和 PASS 命令进行登录"""
        # 发送 USER 命令
        self.send_command(f"USER {username}")
        USER_response = self.receive_response()

        # 检查 USER 命令的响应
        if not USER_response.startswith("331"):
            self.client_output_callback(f"USER command failed: {USER_response}\n")
            sys.exit(1)

        # 发送 PASS 命令
        self.send_command(f"PASS {password}")
        PASS_response = self.receive_response()

        # 检查 PASS 命令的响应
        if not PASS_response.startswith("230"):
            self.client_output_callback(f"PASS command failed: {PASS_response}\n")
            sys.exit(1)
        self.client_output_callback("Logged in successfully.\n")

    def pwd(self):
        """发送 PWD 命令获取当前工作目录"""
        self.send_command("PWD")
        response = self.receive_response()
        if not response.startswith("257"):
            self.client_output_callback(f"PWD command failed: {response}\n")
            return None
        # 解析响应中的路径
        pattern = r'"(.+)"'
        match = re.search(pattern, response)
        if match:
            return match.group(1)
        else:
            self.client_output_callback("[ERROR] Unable to parse PWD response.\n")
            return None

    def cwd(self, directory):
        self.send_command(f"CWD {directory}")
        response = self.receive_response()
        if not response.startswith("250"):
            self.client_output_callback(f"CWD command failed: {response}\n")
        else:
            self.client_output_callback(f"Changed directory to {directory}\n")

    def mkd(self, directory_name):
        self.send_command(f"MKD {directory_name}")
        response = self.receive_response()
        if not response.startswith("257"):
            self.client_output_callback(f"MKD command failed: {response}\n")
        else:
            self.client_output_callback(f"Directory '{directory_name}' created successfully.\n")

    def rmd(self, directory_name):
        self.send_command(f"RMD {directory_name}")
        response = self.receive_response()
        if not response.startswith("250"):
            self.client_output_callback(f"RMD command failed: {response}\n")
        else:
            self.client_output_callback(f"Directory '{directory_name}' removed successfully.\n")

    def enter_port_mode(self, user_input):
        """进入 PORT 模式（主动模式）"""
        pattern = r"PORT (\d+),(\d+),(\d+),(\d+),(\d+),(\d+)"
        match = re.match(pattern, user_input, re.IGNORECASE)
        if not match:
            self.client_output_callback("Invalid PORT command format.\n")
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
            self.client_output_callback(f"Entering PORT mode on {ip_address}:{port}\n")
        except Exception as e:
            self.client_output_callback(f"Failed to enter PORT mode: {e}\n")
            return False
        # 直接将用户输入的 `PORT` 命令发送给服务器
        self.send_command(user_input)
        response = self.receive_response()
        if not response.startswith("200"):
            self.client_output_callback(f"PORT command failed: {response}\n")
            return False
        return True

    def data_connect(self, timeout=5):
        if self.active_mode:
            self.data_socket.settimeout(timeout)
            try:
                connection, addr = self.data_socket.accept()
                self.data_socket.settimeout(None)
                self.client_output_callback(f"Data connection established with {addr}\n")
                return connection
            except socket.timeout:
                self.client_output_callback("Data connection timed out.\n")
                return None
        elif self.pasv_mode:
            try:
                data_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                data_socket.settimeout(timeout)
                data_socket.connect((self.pasv_ip, self.pasv_port))
                data_socket.settimeout(None)
                self.client_output_callback(f"Data connection established to {self.pasv_ip}:{self.pasv_port}\n")
                return data_socket
            except socket.timeout:
                self.client_output_callback("Data connection timed out.\n")
                return None
            except Exception as e:
                self.client_output_callback(f"Data connection failed: {e}\n")
                return None
        else:
            self.client_output_callback("No active or passive mode set for data connection.\n")
            return None

    def list_files(self, directory=None):
        """发送 LIST 命令并接收服务器返回的文件列表"""
        # 防止命令接受出错，间隔1秒发送
        time.sleep(0.5)
        self.enter_pasv_mode()

        if directory:
            self.send_command(f"LIST {directory}")
        else:
            self.send_command("LIST")

        response = self.receive_response()
        if not response.startswith("150"):
            self.client_output_callback(f"LIST command failed: {response}\n")
            return None  # 如果命令失败，返回 None

        data_connection = self.data_connect()
        
        if not data_connection:
            self.client_output_callback("Failed to establish data connection for LIST.\n")
            return None

        data = b""
        while True:
            chunk = data_connection.recv(4096)
            if not chunk:
                break
            data += chunk

        data_connection.close()
        final_response = self.receive_response()
        if not final_response.startswith("226"):
            self.client_output_callback(f"LIST command final response: {final_response}\n")

        # 解析文件列表，并过滤掉 'total' 行
        files = data.decode('utf-8').strip().split('\n')
        # 过滤掉开头包含 "total" 的那一行
        files = [file.strip() for file in files if not file.lower().startswith("total")]
        self.active_mode = False
        self.pasv_mode = False
        return files

    def enter_pasv_mode(self):
        """进入 PASV 模式（被动模式）"""
        self.send_command("PASV")
        response = self.receive_response()
        if not response.startswith("227"):
            self.client_output_callback(f"PASV command failed: {response}\n")
            return False

        pattern = r"227 Entering passive mode \((\d+),(\d+),(\d+),(\d+),(\d+),(\d+)\)"
        match = re.search(pattern, response)
        if not match:
            self.client_output_callback("[ERROR] Unable to parse PASV response.\n")
            return False

        ip_parts = match.groups()[:4]
        p1, p2 = int(match.group(5)), int(match.group(6))
        self.pasv_ip = ".".join(ip_parts)
        self.pasv_port = p1 * 256 + p2

        self.pasv_mode = True
        self.active_mode = False
        self.client_output_callback(f"Entered PASV mode: {self.pasv_ip}:{self.pasv_port}\n")
        return True

    def retr(self, filename):
        if not self.ensure_data_connection():
            self.client_output_callback("Failed to establish a valid data connection for RETR.\n")
            return
        local_filename = filename.split("/")[-1]
        file_offset = 0
        if os.path.exists(local_filename):
            file_offset = os.path.getsize(local_filename)

        if file_offset > 0:
            self.send_command(f"REST {file_offset}")
            rest_response = self.receive_response()
            if not rest_response.startswith("350"):
                self.client_output_callback(f"REST command failed: {rest_response}\n")
                return

        self.send_command(f"RETR {filename}")
        response = self.receive_response() 
        if response.startswith("450"):
            # 处理文件被锁定的情况
            self.client_output_callback(f"RETR command failed: File '{filename}' is locked by another client.\n")
            messagebox.showerror("Error", f"File '{filename}' is currently locked and cannot be downloaded.")
            return
        elif not (response.startswith("150") or response.startswith("125")):
            self.client_output_callback(f"RETR command failed: {response}\n")
            return

        data_socket = self.data_connect()
        if data_socket is None:
            self.client_output_callback("Failed to establish data connection for RETR.\n")
            return

        bytes_received = file_offset
        with open(local_filename, "ab" if file_offset > 0 else "wb") as f:
            while True:
                chunk = data_socket.recv(4096)
                if not chunk:
                    break
                f.write(chunk)
                bytes_received += len(chunk)
                if self.download_callback:
                    self.download_callback(bytes_received, 0)  # 0 as total_size is unknown here

        data_socket.close()
        final_response = self.receive_response()
        if not final_response.startswith("226"):
            self.client_output_callback(f"RETR command final response: {final_response}\n")

        self.active_mode = False
        self.pasv_mode = False
        self.client_output_callback(f"File '{local_filename}' downloaded successfully.\n")

    def stor(self, local_filename):
        if not self.ensure_data_connection():
            self.client_output_callback("Failed to establish a valid data connection for STOR.\n")
            return
        if not os.path.isfile(local_filename):
            self.client_output_callback(f"Local file '{local_filename}' does not exist.\n")
            return

        file_size = os.path.getsize(local_filename)
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
                self.client_output_callback(f"REST command failed: {rest_response}\n")
                return

        filename = os.path.basename(local_filename)
        self.send_command(f"STOR {filename}")
        response = self.receive_response()
        if response.startswith("450"):
            # 处理文件被锁定的情况
            self.client_output_callback(f"STOR command failed: File '{filename}' is locked by another client.\n")
            messagebox.showerror("Error", f"File '{filename}' is currently locked and cannot be uploaded.")
            return
        elif not (response.startswith("150") or response.startswith("125")):
            self.client_output_callback(f"STOR command failed: {response}\n")
            return

        data_socket = self.data_connect()
        if data_socket is None:
            self.client_output_callback("Failed to establish data connection for STOR.\n")
            return

        bytes_sent = file_offset
        with open(local_filename, "rb") as f:
            f.seek(file_offset)
            while True:
                chunk = f.read(4096)
                if not chunk:
                    break
                try:
                    data_socket.sendall(chunk)
                    bytes_sent += len(chunk)
                    if self.upload_callback:
                        self.upload_callback(bytes_sent, file_size)
                except Exception as e:
                    self.client_output_callback(f"Error during upload: {e}\n")
                    messagebox.showerror("Error", f"Failed to upload '{local_filename}': {e}")
                    break

        self.client_output_callback(f"File '{local_filename}' uploaded successfully.\n")
        data_socket.close()
        final_response = self.receive_response()
        if not final_response.startswith("226"):
            self.client_output_callback(f"STOR command final response: {final_response}\n")

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
            self.client_output_callback("Failed to establish a valid data connection for RESUME RETR.\n")
            return
        local_filename = filename.split("/")[-1]
        file_offset = 0
        if os.path.exists(local_filename):
            file_offset = os.path.getsize(local_filename)

        if file_offset > 0:
            self.send_command(f"REST {file_offset}")
            rest_response = self.receive_response()
            if not rest_response.startswith("350"):
                self.client_output_callback(f"REST command failed: {rest_response}\n")
                return

        # 请求文件大小
        self.send_command(f"SIZE {filename}")
        size_response = self.receive_response()
        if not size_response.startswith("213"):
            self.client_output_callback("Failed to get file size for RESUME RETR.\n")
            return
        total_size = int(size_response.split()[1])

        # 发送 RETR 命令
        self.send_command(f"RETR {filename}")
        response = self.receive_response()
        if not (response.startswith("150") or response.startswith("125")):
            self.client_output_callback(f"RETR command failed: {response}\n")
            return

        data_socket = self.data_connect()
        if data_socket is None:
            self.client_output_callback("Failed to establish data connection for RESUME RETR.\n")
            return

        bytes_received = file_offset
        with open(local_filename, "ab") as f:  # 以追加模式打开文件
            while True:
                chunk = data_socket.recv(4096)
                if not chunk:
                    break
                f.write(chunk)
                bytes_received += len(chunk)
                if self.download_callback:
                    self.download_callback(bytes_received, total_size)  # 调用下载进度更新回调

        self.client_output_callback(f"File '{local_filename}' downloaded successfully (resumed).\n")
        data_socket.close()
        final_response = self.receive_response()
        if not final_response.startswith("226"):
            self.client_output_callback(f"RETR command final response: {final_response}\n")
        self.receive_response()
        self.active_mode = False
        self.pasv_mode = False

    def resume_stor(self, local_filename):
        if not self.ensure_data_connection():
            self.client_output_callback("Failed to establish a valid data connection for RESUME STOR.\n")
            return
        if not os.path.isfile(local_filename):
            self.client_output_callback(f"Local file '{local_filename}' does not exist.\n")
            return

        file_size = os.path.getsize(local_filename)

        # 请求服务器上文件大小
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
                self.client_output_callback(f"REST command failed: {rest_response}\n")
                return

        # 发送 STOR 命令
        filename = os.path.basename(local_filename)
        self.send_command(f"STOR {filename}")
        response = self.receive_response()
        if not (response.startswith("150") or response.startswith("125")):
            self.client_output_callback(f"STOR command failed: {response}\n")
            return

        data_socket = self.data_connect()
        if data_socket is None:
            self.client_output_callback("Failed to establish data connection for RESUME STOR.\n")
            return

        bytes_sent = file_offset
        with open(local_filename, "rb") as f:
            f.seek(file_offset)
            while True:
                chunk = f.read(4096)
                if not chunk:
                    break
                data_socket.sendall(chunk)
                bytes_sent += len(chunk)
                if self.upload_callback:
                    self.upload_callback(bytes_sent, file_size)  # 调用上传进度更新回调

        self.client_output_callback(f"File '{local_filename}' uploaded successfully (resumed).\n")
        data_socket.close()
        final_response = self.receive_response()
        if not final_response.startswith("226"):
            self.client_output_callback(f"STOR command final response: {final_response}\n")

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

class FTPClientGUI:
    def __init__(self, master):
        self.master = master
        master.title("FTP Client")

        # 设置主题样式
        self.set_theme()

        # 设置窗口大小和支持缩放
        self.master.geometry("1200x800")  # 调整为更合理的大小
        self.master.minsize(800, 600)

        # 设置全局字体大小
        default_font = ("Arial", 14)  # 字体增大到14
        self.master.option_add("*TButton.Font", default_font)
        self.master.option_add("*TLabel.Font", default_font)
        self.master.option_add("*TEntry.Font", default_font)
        self.master.option_add("*Treeview.Font", default_font)

        # 顶部区域（连接信息和按钮）
        top_frame = ttk.Frame(master)
        top_frame.pack(side=tk.TOP, fill=tk.X, padx=10, pady=5)

        # IP和端口输入框
        self.ip_label = ttk.Label(top_frame, text="Server IP:")
        self.ip_label.pack(side=tk.LEFT, padx=5)
        self.ip_entry = ttk.Entry(top_frame)
        self.ip_entry.pack(side=tk.LEFT, padx=5)

        self.port_label = ttk.Label(top_frame, text="Port:")
        self.port_label.pack(side=tk.LEFT, padx=5)
        self.port_entry = ttk.Entry(top_frame)
        self.port_entry.pack(side=tk.LEFT, padx=5)

        # 用户名和密码输入框
        self.user_label = ttk.Label(top_frame, text="Username:")
        self.user_label.pack(side=tk.LEFT, padx=5)
        self.user_entry = ttk.Entry(top_frame)
        self.user_entry.pack(side=tk.LEFT, padx=5)

        self.pass_label = ttk.Label(top_frame, text="Password:")
        self.pass_label.pack(side=tk.LEFT, padx=5)
        self.pass_entry = ttk.Entry(top_frame, show="*")
        self.pass_entry.pack(side=tk.LEFT, padx=5)

        # 连接和断开按钮
        self.connect_button = self.create_rounded_button("Connect", self.connect, top_frame, width=15, height=2)
        self.connect_button.pack(side=tk.LEFT, padx=5)

        self.disconnect_button = self.create_rounded_button("Disconnect", self.disconnect, top_frame, state=tk.DISABLED, width=15, height=2)
        self.disconnect_button.pack(side=tk.LEFT, padx=5)

        # 当前路径显示区域
        path_frame = ttk.Frame(master)
        path_frame.pack(side=tk.TOP, fill=tk.X, padx=10, pady=5)

        self.path_label = ttk.Label(path_frame, text="Current Path: /")
        self.path_label.pack(side=tk.LEFT, padx=5)

        # 主体区域
        main_frame = ttk.Frame(master)
        main_frame.pack(fill=tk.BOTH, expand=True, padx=10, pady=5)

        # 左侧按钮区域
        left_button_frame = ttk.Frame(main_frame)
        left_button_frame.pack(side=tk.LEFT, fill=tk.Y)

        self.back_button = self.create_rounded_button("Up", self.go_to_parent_directory, left_button_frame, width=15, height=2)
        self.back_button.pack(pady=10)

        self.create_folder_button = self.create_rounded_button("New Folder", self.create_new_folder, left_button_frame, width=15, height=2)
        self.create_folder_button.pack(pady=10)

        # 文件列表区域
        file_frame = ttk.Frame(main_frame)
        file_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.file_tree = ttk.Treeview(file_frame, columns=("name", "type", "size", "modified"), show="headings")
        self.file_tree.heading("name", text="Name")
        self.file_tree.heading("type", text="Type")
        self.file_tree.heading("size", text="Size")
        self.file_tree.heading("modified", text="Last Modified")
        self.file_tree.pack(fill=tk.BOTH, expand=True)
        self.file_tree.bind("<Double-1>", self.on_double_click)

        # 右侧按钮区域
        right_button_frame = ttk.Frame(main_frame)
        right_button_frame.pack(side=tk.LEFT, fill=tk.Y)

        self.port_button = self.create_rounded_button("PORT Mode", self.set_port_mode, right_button_frame, state=tk.DISABLED, width=15, height=2)
        self.port_button.pack(pady=10)

        self.pasv_button = self.create_rounded_button("PASV Mode", self.set_pasv_mode, right_button_frame, state=tk.DISABLED, width=15, height=2)
        self.pasv_button.pack(pady=10)

        # 底部区域（进度条和上传按钮）
        bottom_frame = ttk.Frame(master)
        bottom_frame.pack(side=tk.BOTTOM, fill=tk.X, padx=10, pady=5)

        # 下载进度条和标签
        download_frame = ttk.Frame(bottom_frame)
        download_frame.pack(side=tk.LEFT, fill=tk.X, expand=True)

        self.download_label = ttk.Label(download_frame, text="Download progress: 0%")
        self.download_label.pack(anchor='w', padx=5)

        self.download_progress = ttk.Progressbar(download_frame, orient="horizontal", mode="determinate")
        self.download_progress.pack(fill=tk.X, padx=5)

        # 上传进度条和标签
        upload_frame = ttk.Frame(bottom_frame)
        upload_frame.pack(side=tk.LEFT, fill=tk.X, expand=True)

        self.upload_label = ttk.Label(upload_frame, text="Upload progress: 0%")
        self.upload_label.pack(anchor='w', padx=5)

        self.upload_progress = ttk.Progressbar(upload_frame, orient="horizontal", mode="determinate")
        self.upload_progress.pack(fill=tk.X, padx=5)

        # 上传按钮
        self.upload_button = self.create_rounded_button("Upload File", self.upload_file, bottom_frame, width=15, height=2)
        self.upload_button.pack(side=tk.RIGHT, padx=5)
        # 刷新按钮
        self.refresh_button = self.create_rounded_button("Refresh", self.load_file_list, bottom_frame, width=15, height=2)
        self.refresh_button.pack(side=tk.RIGHT, padx=5)
        
        # 输出区域
        output_frame = ttk.Frame(master)
        output_frame.pack(side=tk.BOTTOM, fill=tk.BOTH, expand=True, padx=10, pady=5)

        # 客户端输出
        client_output_frame = ttk.LabelFrame(output_frame, text="Client Output")
        client_output_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5, pady=5)

        self.client_output_text = tk.Text(client_output_frame, wrap=tk.WORD, state=tk.DISABLED, height=10)
        self.client_output_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        client_scrollbar = ttk.Scrollbar(client_output_frame, command=self.client_output_text.yview)
        client_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.client_output_text['yscrollcommand'] = client_scrollbar.set

        # 服务器输出
        server_output_frame = ttk.LabelFrame(output_frame, text="Server Output")
        server_output_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5, pady=5)

        self.server_output_text = tk.Text(server_output_frame, wrap=tk.WORD, state=tk.DISABLED, height=10)
        self.server_output_text.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)

        server_scrollbar = ttk.Scrollbar(server_output_frame, command=self.server_output_text.yview)
        server_scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        self.server_output_text['yscrollcommand'] = server_scrollbar.set

        # FTP客户端实例
        self.ftp_client = None
        self.mode = None

    def set_theme(self):
        """设置浅色主题"""
        style = ttk.Style(self.master)
        style.theme_use('default')  # 使用浅色主题
        style.configure('TButton', background='#DDDDDD', foreground='#000000', relief='raised', padding=10, borderwidth=2)
        style.configure('TLabel', background='#F0F0F0', foreground='#000000')
        style.configure('Treeview', background='#F0F0F0', fieldbackground='#FFFFFF', foreground='#000000')
        style.configure('TEntry', background='#FFFFFF', foreground='#000000')
        style.configure('TFrame', background='#F0F0F0')
        style.configure('Labelframe.Label', background='#F0F0F0', foreground='#000000')

        self.master.configure(bg='#F0F0F0')  # 设置主背景色为浅色

    def create_rounded_button(self, text, command, master, width=10, height=1, **kwargs):
        """创建带圆角效果的按钮，并调整按钮大小"""
        button = ttk.Button(master, text=text, command=command, width=width, **kwargs)
        return button

    def connect(self):
        ip = self.ip_entry.get()
        port = self.port_entry.get()
        username = self.user_entry.get()
        password = self.pass_entry.get()

        if not ip or not port or not username or not password:
            messagebox.showerror("Error", "Please enter valid IP, port, username, and password")
            return

        try:
            # 使用后台线程连接避免阻塞UI
            threading.Thread(target=self.connect_to_server, args=(ip, int(port), username, password), daemon=True).start()
        except Exception as e:
            messagebox.showerror("Error", str(e))

    def connect_to_server(self, ip, port, username, password):
        # 创建FTP客户端并连接到服务器，同时传递输出回调函数
        self.ftp_client = FTP_CLIENT(ip, port, self.update_download_progress, self.update_upload_progress,
                                     self.append_client_output, self.append_server_output)
        self.ftp_client.connect()

        # 登录服务器
        self.ftp_client.login(username, password)
        
        # 登录成功后，加载文件列表并启用模式按钮
        self.load_file_list()

        self.connect_button.config(state=tk.DISABLED)
        self.disconnect_button.config(state=tk.NORMAL)
        self.port_button.config(state=tk.NORMAL)
        self.pasv_button.config(state=tk.NORMAL)

    def disconnect(self):
        # 发送QUIT命令并关闭连接

        if self.ftp_client:
            # self.ftp_client.send_command("QUIT")
            self.ftp_client.close()
            self.ftp_client = None
            self.connect_button.config(state=tk.NORMAL)
            self.disconnect_button.config(state=tk.DISABLED)
            self.port_button.config(state=tk.DISABLED)
            self.pasv_button.config(state=tk.DISABLED)
            self.path_label.config(text="Current Path: /")
    def clear_output(self):
        self.client_output_text.config(state=tk.NORMAL)
        self.client_output_text.delete(1.0, tk.END)
        self.client_output_text.config(state=tk.DISABLED)

        self.server_output_text.config(state=tk.NORMAL)
        self.server_output_text.delete(1.0, tk.END)
        self.server_output_text.config(state=tk.DISABLED)

    def append_client_output(self, text):
        def append():
            self.client_output_text.config(state=tk.NORMAL)
            self.client_output_text.insert(tk.END, text)
            self.client_output_text.see(tk.END)
            self.client_output_text.config(state=tk.DISABLED)
        self.master.after(0, append)

    def append_server_output(self, text):
        def append():
            self.server_output_text.config(state=tk.NORMAL)
            self.server_output_text.insert(tk.END, text)
            self.server_output_text.see(tk.END)
            self.server_output_text.config(state=tk.DISABLED)
        self.master.after(0, append)

    def load_file_list(self):
        # 清除旧的文件列表
        for item in self.file_tree.get_children():
            self.file_tree.delete(item)

        # 获取服务器文件列表
        files = self.ftp_client.list_files()  # list_files 方法返回文件信息的列表
        if files is None:
            return

        # 获取当前路径
        current_path = self.ftp_client.pwd()
        if current_path:
            self.path_label.config(text=f"Current Path: {current_path}")
        else:
            self.path_label.config(text="Current Path: /")

        if len(files) == 0:  # 处理空文件夹的情况
            # 这里不弹出错误，只是不显示任何内容
            return

        # 解析并添加文件信息到Treeview
        for file in files:
            file_info = self.parse_file_info(file)
            self.file_tree.insert("", "end", values=(file_info["name"], file_info["type"], file_info["size"], file_info["modified"]))

    def parse_file_info(self, file):
        # 假设服务器返回的文件信息类似于：drwxr-xr-x  2 user group 4096 Oct 14 2024 folder_name
        parts = file.split()
        file_type = "Directory" if file.startswith("d") else "File"
        file_size = parts[4]
        file_modified = " ".join(parts[5:8])
        file_name = parts[8]

        return {"name": file_name, "type": file_type, "size": file_size, "modified": file_modified}

    def set_port_mode(self):
        """切换到PORT模式并允许用户输入端口号"""
        if self.ftp_client:
            # 弹出输入框让用户输入端口号
            port_input = simpledialog.askstring("PORT Mode", "Enter port number:")
            if port_input:
                try:
                    # 将输入的端口号转为整数，并生成IP地址和端口组合
                    port_number = int(port_input)
                    # 假设使用本地的IP地址作为主动模式的IP，可以用127.0.0.1替代
                    user_ip = "127,0,0,1"
                    # 将端口号转换为两个8位的数字表示
                    p1 = port_number // 256
                    p2 = port_number % 256
                    port_command = f"PORT {user_ip},{p1},{p2}"
                    if self.ftp_client.enter_port_mode(port_command):  # 调用FTP客户端的PORT模式
                        self.mode = "PORT"
                        messagebox.showinfo("Info", f"Switched to PORT mode with port {port_number}.")
                    else:
                        messagebox.showerror("Error", "Failed to switch to PORT mode.")
                except ValueError:
                    messagebox.showerror("Error", "Invalid port number.")

    def set_pasv_mode(self):
        """切换到PASV模式"""
        if self.ftp_client:
            if self.ftp_client.enter_pasv_mode():
                self.mode = "PASV"
                messagebox.showinfo("Info", "Switched to PASV mode.")
            else:
                messagebox.showerror("Error", "Failed to switch to PASV mode.")

    def on_double_click(self, event):
        # 获取选中的文件
        selected_item = self.file_tree.selection()[0]  # 获取选中的行
        file_name = self.file_tree.item(selected_item)['values'][0]  # 获取文件名

        # 判断是否是目录，如果是则进入目录
        file_type = self.file_tree.item(selected_item)['values'][1]
        if file_type == "Directory":
            self.ftp_client.cwd(file_name)  # 进入选中的目录
            self.load_file_list()  # 进入目录后刷新文件列表
        else:
            threading.Thread(target=self.download_thread, args=(file_name,), daemon=True).start()

    def go_to_parent_directory(self):
        self.ftp_client.cwd("..")  # 进入上一级目录
        self.load_file_list()  # 刷新文件列表

    def create_new_folder(self):
        folder_name = simpledialog.askstring("New Folder", "Enter folder name:")  # 弹出输入框
        if folder_name:
            self.ftp_client.mkd(folder_name)  # 创建新文件夹
            self.load_file_list()  # 刷新文件列表

    def download_thread(self, filename):
        # 如果没有ensure_data_connection，说明没有建立有效的数据连接 弹窗错误
        if not self.ftp_client.ensure_data_connection():
            messagebox.showerror("Error", "Failed to establish a valid data connection")
            return
        self.download_progress['value'] = 0  # 重置进度条
        self.download_label.config(text="Download progress: 0%")
        threading.Thread(target=self.ftp_client.resume_retr, args=(filename,), daemon=True).start()


    def update_download_progress(self, current, total):
        def gui_update():
            progress = (current / total) * 100
            self.download_progress['value'] = progress
            self.download_label.config(text=f"Download progress: {progress:.2f}%")
            # 如果下载完成，重置进度条和刷新文件列表
            if progress >= 100:
                self.download_progress['value'] = 0
                self.download_label.config(text="Download progress: 0%")
                # time.sleep(1)
                # self.load_file_list()
        # 使用 after 方法在主线程中执行 GUI 更新
        self.master.after(0, gui_update)

    def upload_file(self):
        # 如果没有ensure_data_connection，说明没有建立有效的数据连接 弹窗错误
        if not self.ftp_client.ensure_data_connection():
            messagebox.showerror("Error", "Failed to establish a valid data connection")
            return
        local_filename = filedialog.askopenfilename()
        if local_filename:
            self.upload_progress['value'] = 0  # 重置进度条
            self.upload_label.config(text="Upload progress: 0%")
            threading.Thread(target=self.upload_thread, args=(local_filename,), daemon=True).start()

    def upload_thread(self, local_filename):
        self.ftp_client.resume_stor(local_filename)


    def update_upload_progress(self, current, total):
        def gui_update():
            progress = (current / total) * 100
            self.upload_progress['value'] = progress
            self.upload_label.config(text=f"Upload progress: {progress:.2f}%")
            # 如果上传完成，重置进度条和刷新文件列表
            if progress >= 100:
                self.upload_progress['value'] = 0
                self.upload_label.config(text="Upload progress: 0%")
                #time.sleep(1)
                # self.load_file_list()
        # 使用 after 方法在主线程中执行 GUI 更新
        self.master.after(0, gui_update)

    def on_right_click(self, event):
        selected_item = self.file_tree.selection()
        if selected_item:
            menu = tk.Menu(self.master, tearoff=0)
            menu.add_command(label="Delete Folder", command=self.delete_folder)
            menu.post(event.x_root, event.y_root)

    def delete_folder(self):
        selected_item = self.file_tree.selection()[0]
        folder_name = self.file_tree.item(selected_item)['values'][0]
        # 确认删除操作
        if messagebox.askyesno("Delete Folder", f"Are you sure you want to delete folder '{folder_name}'?"):
            self.ftp_client.rmd(folder_name)  # 删除文件夹
            self.load_file_list()  # 刷新文件列表
def main():
    root = ThemedTk(theme="breeze") 
    root.title("FTP Client")
    ftp_client_gui = FTPClientGUI(root)
    
    root.mainloop()

if __name__ == "__main__":
    main()
