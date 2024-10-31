# How to write a chat program (two clients chat with each other) with UDP?

# Answer:

**1. Server:**

· Create a UDP socket.

· Bind to a port to listen for incoming messages.

· Maintain a list of connected clients, such as clients{}.

· Transfer messages from one client to another.

- Code: 

```python
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', 9876))

clients = {}  # 存储客户端地址

try:
    while True:
        data, address = sock.recvfrom(8192)
        if address not in clients:
            clients[address] = address  # 记录新的客户端地址
        # 转发消息给其他客户端
        for client in clients:
            if client != address:  # 不发送给发送方
                sock.sendto(data, client)
finally:
    sock.close()

```

**2. Client:**

· Create a UDP socket.

· Send messages to the server.

· Receive messages from the server.

- Code: 

```python
import socket
import threading

def receive_messages(sock):
    while True:
        data, _ = sock.recvfrom(8192)
        print("Received:", data.decode())

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(('', 0))  # 自动分配端口

threading.Thread(target=receive_messages, args=(sock,), daemon=True).start()

try:
    while True:
        msg = input("Enter message: ")
        sock.sendto(msg.encode(), ('localhost', 9876))  # 发送到服务器
finally:
    sock.close()

```

