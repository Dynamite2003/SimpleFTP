/*服务器搭建流程总结：
创建套接字和地址结构：

建立 serverSocket 套接字和服务器地址结构 server_addr。
如果需要，还创建客户端地址结构 clientAddr。
初始化：

初始化服务器地址结构和其他必要的设置（如端口号和根目录）。
绑定和监听：

将 serverSocket 绑定到服务器地址结构，并设置为监听状态，准备接受客户端连接。
循环等待连接：

进入一个无限循环，使用 accept 函数等待客户端连接。
一旦连接成功，保存客户端的 IP 和端口信息，以及返回的 client 套接字。
创建子进程：

在成功连接后，父进程创建一个子进程来处理与该客户端的交互。
关闭套接字：

父进程关闭 client 套接字，以便继续等待其他客户端的连接，而保留 serverSocket。
子进程关闭 serverSocket，并使用 client 套接字进行数据传输。
数据处理：

子进程轮询接收客户端命令，调用处理函数进行相应处理。
关闭连接：

在完成数据交换后，关闭所有相关的套接字，释放资源，优雅地结束连接。*/
#include "server.h"
#include <signal.h>

#define BACKLOG 3
#define WELCOME_MSG "220 ftp.ssast.org FTP server ready.\r\n"
#define ROOT_DIRECTORY "/mnt/d/ftp_data"

void sigchld_handler(int s)
{
    // 等待所有子进程结束，避免僵尸进程
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
}

void create_server(int port, const char *root_dir)
{
    // 创建服务器套接字和地址结构
    int serverSocket;
    struct sockaddr_in server_addr;

    // 创建客户端套接字和地址结构
    int clientSocket;
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // 初始化服务器地址结构
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // 创建服务器套接字
    if ((serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("socket");
        return;
    }

    // 绑定服务器套接字
    if (bind(serverSocket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("bind");
        close(serverSocket);
        return;
    }

    // 监听服务器套接字
    if (listen(serverSocket, BACKLOG) < 0)
    {
        perror("listen");
        close(serverSocket);
        return;
    }

    // 处理 SIGCHLD 信号，清理子进程
    struct sigaction sa;
    sa.sa_handler = sigchld_handler; // 注册处理函数
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP; // 重新启动被信号中断的系统调用
    sigaction(SIGCHLD, &sa, NULL);

    // 输出服务器的启动信息
    printf("Server started on port %d\n", port);
    printf("Serving files from root directory: %s\n", root_dir);

    // 循环等待链接
    while (1)
    {
        // 接受客户端链接
        if ((clientSocket = accept(serverSocket, (struct sockaddr *)&client_addr, &addr_len)) < 0)
        {
            perror("accept");
            continue;
        }

        // 创建子进程
        int pid = fork();
        if (pid < 0)
        {
            perror("fork");
            close(clientSocket);
            continue; // 继续等待其他连接
        }

        // 子进程，处理客户端请求
        if (pid == 0)
        {
            // 关闭父进程的 serverSocket
            close(serverSocket);

            // 创建服务端状态结构体，并分配内存
            FtpState *state = (FtpState *)malloc(sizeof(FtpState));
            if (state == NULL)
            {
                perror("Failed to allocate memory for FtpState");
                exit(1); // 内存分配失败，退出子进程
            }

            // 初始化 FtpState 的各个字段
            state->state = STATE_WAITING_USER; // 状态设置为等待用户登录
            strcpy(state->username, "");       // 初始化用户名为空字符串
            strcpy(state->password, "");       // 初始化密码为空字符串
            state->port_ip[0] = '\0';          // PORT 命令指定的 IP 地址初始化为空
            state->port_port = 0;              // PORT 命令指定的端口初始化为 0
            state->pasv_socket = -1;           // 被动模式的监听套接字初始化为 -1
            state->pasv_port = 0;              // 被动模式的监听端口初始化为 0
            state->data_connection_active = 0; // 数据连接状态初始化为未激活

            // 文件系统相关字段初始化
            strcpy(state->root_dir, root_dir); // 使用命令行传入的根目录
            strcpy(state->current_dir, "/");   // 初始时将当前目录设为根目录 "/"

            // 调试信息输出
            printf("[DEBUG] Initialized FtpState for new connection. Root directory: %s, Current directory: %s\n",
                   state->root_dir, state->current_dir);

            char input_msg[MAXSIZE];
            int bytes_received;

            // 向客户端发送欢迎消息
            send(clientSocket, WELCOME_MSG, strlen(WELCOME_MSG), 0);

            // 轮询接收客户端命令
            while ((bytes_received = recv(clientSocket, input_msg, sizeof(input_msg) - 1, 0)) > 0)
            {
                input_msg[bytes_received] = '\0';                            // 添加终止符
                respond_to_client(input_msg, clientSocket, root_dir, state); // 处理客户端请求
            }

            // 处理完毕，关闭客户端套接字
            close(clientSocket);
            exit(0); // 子进程结束
        }
        else
        {
            // 父进程，关闭 client 套接字
            close(clientSocket);
        }
    }

    // 关闭服务器套接字
    close(serverSocket);
}

int main(int argc, char *argv[])
{
    int port = 21;               // 默认端口号
    char root_dir[256] = "/tmp"; // 默认根目录

    // 如果传入了命令行参数
    if (argc > 1)
    {
        for (int i = 1; i < argc; i++)
        {
            // 解析 -port 参数
            if (strcmp(argv[i], "-port") == 0 && i + 1 < argc)
            {
                port = atoi(argv[i + 1]); // 转换为整数
            }

            // 解析 -root 参数
            if (strcmp(argv[i], "-root") == 0 && i + 1 < argc)
            {
                strncpy(root_dir, argv[i + 1], sizeof(root_dir) - 1);
                root_dir[sizeof(root_dir) - 1] = '\0'; // 确保字符串以 null 结尾
            }
        }
    }

    // 检查端口号是否合法
    if (port <= 0 || port > 65535)
    {
        fprintf(stderr, "Invalid port number: %d\n", port);
        return 1;
    }

    // 输出调试信息
    printf("Starting server on port: %d\n", port);
    printf("Serving files from root directory: %s\n", root_dir);

    // 调用 create_server 函数，并将解析得到的端口号和根目录传递进去
    create_server(port, root_dir);

    return 0;
}
