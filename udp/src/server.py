import socket

size = 8192
# 添加sequence_num
sequence_num = 0


sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', 9876))

try:
    while True:
        data, address = sock.recvfrom(size)
        response = f"{sequence_num} {data.decode().upper()}"  # 组合序列号和消息
        sock.sendto(response.encode(), address)  # 将接收到的数据转换为大写并发送回去
        sequence_num+=1
finally:
    sock.close()
