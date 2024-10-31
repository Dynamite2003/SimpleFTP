import socket

size = 8192

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

try:
    for i in range(51):  # 发送0到50的消息
        msg = str(i)  # 将数字转换为字符串
        sock.sendto(msg.encode(), ('localhost', 9876))  # 发送到服务器
        response = sock.recv(size).decode()  # 接收服务器的响应
        print(response)  # 打印返回的消息
finally:
    sock.close()
