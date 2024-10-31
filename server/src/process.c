#include "server.h"
// 保存用户名和密码的函数
// 从文件中加载用户信息
int check_user_credentials(const char *username, const char *password)
{
    FILE *file = fopen(USER_DB_FILE, "r");
    if (file == NULL)
    {
        // perror("[ERROR] Failed to open user database");
        return 0; // 文件不存在表示没有任何用户
    }

    char line[256];
    char stored_user[100], stored_pass[100];
    while (fgets(line, sizeof(line), file))
    {
        // 解析用户名和密码
        sscanf(line, "%99[^:]:%99s", stored_user, stored_pass);
        if (strcmp(username, stored_user) == 0)
        {
            if (strcmp(password, stored_pass) == 0)
            {
                fclose(file);
                return 1; // 用户名和密码匹配
            }
            else
            {
                fclose(file);
                return 0; // 密码错误
            }
        }
    }
    fclose(file);
    return -1; // 用户不存在
}

// 保存新用户信息
int save_new_user(const char *username, const char *password)
{
    FILE *file = fopen(USER_DB_FILE, "a"); // 使用追加模式打开文件
    if (file == NULL)
    {
        perror("[ERROR] Failed to open user database for writing");
        return 0;
    }

    fprintf(file, "%s:%s\n", username, password);
    fclose(file);
    return 1;
}

// 工具函数：规范化路径，去除 ".." 等非法路径
void normalize_path(char *path)
{
    char *src = path;
    char *dst = path;

    while (*src)
    {
        if (src[0] == '/' && src[1] == '/')
        {
            // 跳过连续的 '/'
            src++;
        }
        else if (src[0] == '/' && src[1] == '.' && src[2] == '/')
        {
            // 跳过 "/./"
            src += 2;
        }
        else if (src[0] == '/' && src[1] == '.' && src[2] == '.' && (src[3] == '/' || src[3] == '\0'))
        {
            // 处理 "/../"
            if (dst > path)
            {
                // 找到上一个 '/'
                dst--;
                while (dst > path && *dst != '/')
                    dst--;
            }
            src += 3;
        }
        else
        {
            // 正常路径字符拷贝
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}
// 建立数据连接函数
int setup_data_connection(int clientSocket, FtpState *state)
{
    int dataSocket = -1;

    // 被动模式
    if (state->pasv_socket > 0)
    {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        dataSocket = accept(state->pasv_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (dataSocket < 0)
        {
            perror("[ERROR] accept");
            return -1;
        }
        close(state->pasv_socket);
        state->pasv_socket = -1;
    }
    // 主动模式
    else if (state->port_port > 0)
    {
        struct sockaddr_in data_addr;
        dataSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (dataSocket < 0)
        {
            // perror("[ERROR] socket");
            return -1;
        }
        memset(&data_addr, 0, sizeof(data_addr));
        data_addr.sin_family = AF_INET;
        data_addr.sin_port = htons(state->port_port);
        inet_pton(AF_INET, state->port_ip, &data_addr.sin_addr);

        if (connect(dataSocket, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0)
        {
            // perror("[ERROR] connect");
            close(dataSocket);
            return -1;
        }
    }
    else
    {
        // printf("[ERROR] No valid data connection mode.\n");
        return -1;
    }

    return dataSocket;
}

// 修改后的 USER 命令处理函数
void ftp_user(int clientSocket, FtpState *state, const char *username)
{
    // printf("[DEBUG] USER command received with username: %s\n", username);
    strcpy(state->username, username);

    if (strcmp(username, "anonymous") == 0)
    {
        // 允许匿名用户登录
        send(clientSocket, "331 Please specify the password.\r\n", 34, 0);
        state->state = STATE_WAITING_PASS;
        // printf("[DEBUG] Username accepted: %s, waiting for password.\n", username);
    }
    else
    {
        // 如果不是匿名用户，则检查用户是否存在并等待密码
        send(clientSocket, "331 Username accepted, please specify the password.\r\n", 53, 0);
        state->state = STATE_WAITING_PASS;
        // printf("[DEBUG] Username accepted: %s, waiting for password.\n", username);
    }
}

// 处理 PASS 命令
void ftp_pass(int clientSocket, FtpState *state, const char *password)
{
    // printf("[DEBUG] PASS command received with password: %s\n", password);

    // 存储密码
    strcpy(state->password, password);
    // 检查用户名是否存在于数据库中
    int result = check_user_credentials(state->username, password);

    if (result == 1)
    {
        state->state = STATE_CLIENT_LOGIN;
        // printf("[DEBUG] Password matched for user: %s\n", state->username);
    }
    else if (result == 0)
    {
        // 密码错误
        send(clientSocket, "530 Login incorrect.\r\n", 23, 0);
        // printf("[DEBUG] Incorrect password for user: %s\n", state->username);
    }
    else if (result == -1)
    {
        // 新用户，保存用户名和密码
        if (save_new_user(state->username, password))
        {
            state->state = STATE_CLIENT_LOGIN;
            printf("[DEBUG] New user created: %s\n", state->username);
        }
        else
        {
            send(clientSocket, "550 Failed to create new user.\r\n", 33, 0);
        }
    }
    if (state->state == STATE_CLIENT_LOGIN)
    { // 准备发送的欢迎消息
        char *pass_msg =
            "230-Welcome to\r\n"
            "230- School of Software\r\n"
            "230- FTP Archives at ftp.ssast.org\r\n"
            "230-\r\n"
            "230-This site is provided as a public service by School of\r\n"
            "230-Software. Use in violation of any applicable laws is strictly\r\n"
            "230-prohibited. We make no guarantees, explicit or implicit, about the\r\n"
            "230-contents of this site. Use at your own risk.\r\n"
            "230-\r\n"
            "230 Guest login ok, access restrictions apply.\r\n";

        // 发送欢迎消息
        send(clientSocket, pass_msg, strlen(pass_msg), 0);
        printf("[DEBUG] Password accepted. User logged in.\n");
    }
}

// 实现 ftp_quit 函数
void ftp_quit(int clientSocket, FtpState *state)
{
    printf("[DEBUG] QUIT command received. Closing connection.\n");

    // 发送退出消息
    char *msg = "221-Thank you for using the FTP service on ftp.ssast.org.\r\n"
                "221 Goodbye.\r\n ";
    send(clientSocket, msg, strlen(msg), 0);

    // 关闭客户端套接字
    printf("[DEBUG] Closing client socket: %d\n", clientSocket);
    close(clientSocket);

    // 退出子进程
    exit(0);
}

// 实现 ftp_abor 函数
void ftp_abor(int clientSocket, FtpState *state)
{
    printf("[DEBUG] ABOR command received. Aborting current operation.\n");

    // 发送中止消息
    char *msg = "226 ABOR command successful.\r\n";
    send(clientSocket, msg, strlen(msg), 0);
}

// 实现 ftp_port 函数
void ftp_port(int clientSocket, FtpState *state, char *parameter)
{
    int a1, a2, a3, a4, p1, p2;
    char ip_address[50];
    int port;

    printf("[DEBUG] PORT command received with parameter: %s\n", parameter);

    // 重置被动模式相关的套接字和端口号
    if (state->pasv_socket > 0)
    {
        printf("[DEBUG] Closing existing PASV socket: %d\n", state->pasv_socket);
        close(state->pasv_socket);
        state->pasv_socket = -1;
        state->pasv_port = 0;
    }

    // 解析 PORT 命令中的参数，格式：h1,h2,h3,h4,p1,p2
    sscanf(parameter, "%d,%d,%d,%d,%d,%d", &a1, &a2, &a3, &a4, &p1, &p2);

    // 构造 IP 地址字符串
    sprintf(ip_address, "%d.%d.%d.%d", a1, a2, a3, a4);
    port = p1 * 256 + p2; // 计算端口号：p1 * 256 + p2

    printf("[DEBUG] Parsed IP: %s, Port: %d\n", ip_address, port);

    // 保存到状态结构体中
    strcpy(state->port_ip, ip_address);
    state->port_port = port;

    // 标记当前有效的 PORT 连接
    state->data_connection_active = 1;

    // 返回成功消息
    char msg[MAXSIZE];
    sprintf(msg, "200 PORT command successful. Connection established to %s:%d.\r\n", ip_address, port);
    send(clientSocket, msg, strlen(msg), 0);

    printf("[DEBUG] PORT command processed successfully. Data connection set to %s:%d\n", ip_address, port);
}

// 实现 ftp_pasv 函数
void ftp_pasv(int clientSocket, FtpState *state)
{
    struct sockaddr_in pasv_addr;
    int addr_len = sizeof(pasv_addr);
    char server_ip[50];
    char msg[MAXSIZE];
    int p1, p2;

    printf("[DEBUG] PASV command received. Initiating passive mode.\n");

    // 关闭上一次打开的 PASV 端口（如果有）
    if (state->pasv_socket > 0)
    {
        printf("[DEBUG] Closing previous PASV socket: %d\n", state->pasv_socket);
        close(state->pasv_socket);
    }

    // 重置 PORT 相关的状态
    state->port_port = 0;
    memset(state->port_ip, 0, sizeof(state->port_ip));

    // 创建新的 PASV 套接字
    state->pasv_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (state->pasv_socket < 0)
    {
        perror("socket");
        sprintf(msg, "421 Failed to create PASV socket.\r\n");
        send(clientSocket, msg, strlen(msg), 0);
        return;
    }

    // 初始化被动模式地址结构体
    memset(&pasv_addr, 0, sizeof(pasv_addr));
    pasv_addr.sin_family = AF_INET;
    pasv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 使用服务器本机的 IP 地址

    // 随机选择一个端口号 (20000 到 65535)
    srand(time(NULL)); // 初始化随机种子
    state->pasv_port = 20000 + rand() % (65535 - 20000 + 1);
    pasv_addr.sin_port = htons(state->pasv_port);

    printf("[DEBUG] Attempting to bind PASV socket to port: %d\n", state->pasv_port);

    // 绑定套接字到选择的端口号
    if (bind(state->pasv_socket, (struct sockaddr *)&pasv_addr, sizeof(pasv_addr)) < 0)
    {
        perror("bind");
        sprintf(msg, "421 Failed to bind PASV socket.\r\n");
        send(clientSocket, msg, strlen(msg), 0);
        return;
    }

    // 开始监听
    if (listen(state->pasv_socket, 1) < 0)
    {
        perror("listen");
        sprintf(msg, "421 Failed to listen on PASV socket.\r\n");
        send(clientSocket, msg, strlen(msg), 0);
        return;
    }

    // 获取服务器的 IP 地址
    if (getsockname(clientSocket, (struct sockaddr *)&pasv_addr, (socklen_t *)&addr_len) < 0)
    {
        perror("getsockname");
        sprintf(msg, "421 Failed to get server IP address.\r\n");
        send(clientSocket, msg, strlen(msg), 0);
        return;
    }
    inet_ntop(AF_INET, &pasv_addr.sin_addr, server_ip, sizeof(server_ip));

    printf("[DEBUG] Server IP address for PASV mode: %s\n", server_ip);

    // 将 IP 地址转换为逗号分隔格式，例如：192.168.1.1 -> 192,168,1,1
    for (int i = 0; i < strlen(server_ip); i++)
    {
        if (server_ip[i] == '.')
            server_ip[i] = ',';
    }

    // 计算 p1 和 p2
    p1 = state->pasv_port / 256;
    p2 = state->pasv_port % 256;

    // 标记当前有效的 PASV 连接
    state->data_connection_active = 1;

    // 构造 227 响应消息
    sprintf(msg, "227 Entering passive mode (%s,%d,%d).\r\n", server_ip, p1, p2);
    send(clientSocket, msg, strlen(msg), 0);

    printf("[DEBUG] PASV command processed successfully. Server listening on %s:%d\n", server_ip, state->pasv_port);
}

// RETR 命令的实现
// RETR 命令的实现
void ftp_retr(int clientSocket, FtpState *state, const char *filename)
{
    printf("[DEBUG] RETR command received with filename: %s\n", filename);
    FILE *file;
    char buffer[MAXSIZE];
    char full_path[MAXSIZE];
    int dataSocket;
    int bytes_read;

    snprintf(full_path, sizeof(full_path), "%s%s/%s", state->root_dir, strcmp(state->current_dir, "/") == 0 ? "" : state->current_dir, filename);

    printf("[DEBUG] RETR command received. Full path to retrieve: %s\n", full_path);

    file = fopen(full_path, "rb");
    if (file == NULL)
    {
        perror("fopen");
        send(clientSocket, "550 File not found or access denied.\r\n", 37, 0);
        return;
    }

    if (state->data_connection_active == 0)
    {
        send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
        fclose(file);
        return;
    }

    send(clientSocket, "150 Opening BINARY mode data connection.\r\n", 42, 0);

    // Handle passive or active mode data connection
    if (state->pasv_socket > 0)
    {

        printf("[DEBUG] Waiting for client to connect to PASV port: %d\n", state->pasv_port);
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        dataSocket = accept(state->pasv_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (dataSocket < 0)
        {
            perror("accept");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            fclose(file);
            return;
        }
        close(state->pasv_socket);
        state->pasv_socket = -1;
    }
    else
    {
        printf("[DEBUG] Initiating active mode connection to: %s:%d\n", state->port_ip, state->port_port);

        struct sockaddr_in data_addr;
        memset(&data_addr, 0, sizeof(data_addr));
        data_addr.sin_family = AF_INET;
        data_addr.sin_port = htons(state->port_port);
        inet_pton(AF_INET, state->port_ip, &data_addr.sin_addr);

        dataSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (dataSocket < 0)
        {
            perror("socket");
            printf("[DEBUG] Error creating socket for active mode data connection.\n");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            fclose(file);
            return;
        }

        if (connect(dataSocket, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0)
        {
            printf("[DEBUG] Error connecting to client for active mode data connection.\n");
            perror("connect");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            close(dataSocket);
            fclose(file);
            return;
        }
    }

    if (state->transfer_offset > 0)
    {
        fseek(file, state->transfer_offset, SEEK_SET);
        state->transfer_offset = 0;
    }

    state->is_transferring = 1;
    printf("[DEBUG] Starting file transfer...\n");

    while ((bytes_read = fread(buffer, 1, MAXSIZE, file)) > 0)
    {
        int total_sent = 0;
        while (total_sent < bytes_read)
        {
            int sent = send(dataSocket, buffer + total_sent, bytes_read - total_sent, 0);
            if (sent < 0)
            {
                perror("send");
                send(clientSocket, "426 Connection closed; transfer aborted.\r\n", 43, 0);
                fclose(file);
                close(dataSocket);
                state->is_transferring = 0;
                return;
            }
            total_sent += sent;
        }
    }
    printf("[DEBUG] File transfer complete.\n");
    // Reset transferring flag
    state->is_transferring = 0;
    fclose(file);

    shutdown(dataSocket, SHUT_WR);

    close(dataSocket);

    char response[256];
    memset(response, 0, sizeof(response)); // 初始化整个缓冲区为0
    snprintf(response, sizeof(response), "226 Transfer complete.\r\n");

    send(clientSocket, response, strlen(response), 0);
    printf("[DEBUG] Data connection closed.\n");
}

void ftp_stor(int clientSocket, FtpState *state, char *filename)
{
    char full_path[MAXSIZE];
    FILE *file;
    char buffer[MAXSIZE];
    int bytes_read;
    int dataSocket;

    snprintf(full_path, sizeof(full_path), "%s%s/%s", state->root_dir, strcmp(state->current_dir, "/") == 0 ? "" : state->current_dir, filename);

    printf("[DEBUG] STOR command received. Full path to save file: %s\n", full_path);

    if (state->transfer_offset > 0)
    {
        file = fopen(full_path, "rb+");
        if (file == NULL)
        {
            perror("[ERROR] fopen");
            send(clientSocket, "550 Failed to open file.\r\n", 27, 0);
            return;
        }
        fseek(file, 0, SEEK_END);
        long file_size = ftell(file);

        if (state->transfer_offset > file_size)
        {
            send(clientSocket, "501 Invalid offset.\r\n", 22, 0);
            fclose(file);
            return;
        }
        fseek(file, state->transfer_offset, SEEK_SET);
        printf("[DEBUG] Resuming upload at offset: %ld\n", state->transfer_offset);
    }
    else
    {
        file = fopen(full_path, "wb");
        if (file == NULL)
        {
            perror("[ERROR] fopen");
            send(clientSocket, "550 Failed to open file.\r\n", 27, 0);
            return;
        }
    }

    int file_fd = fileno(file);
    if (flock(file_fd, LOCK_EX | LOCK_NB) != 0)
    {
        perror("[ERROR] flock");
        send(clientSocket, "550 File is currently being written by another client.\r\n", 55, 0);
        fclose(file);
        return;
    }

    send(clientSocket, "150 Opening BINARY mode data connection.\r\n", 42, 0);

    // Handle passive or active mode data connection
    if (state->pasv_socket > 0)
    {
        printf("[DEBUG] Waiting for client to connect to PASV port: %d\n", state->pasv_port);
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        dataSocket = accept(state->pasv_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (dataSocket < 0)
        {
            perror("[ERROR] accept");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            fclose(file);
            return;
        }
        close(state->pasv_socket);
        state->pasv_socket = -1;
    }
    else if (state->port_port > 0)
    {
        printf("[DEBUG] Initiating active mode connection to: %s:%d\n", state->port_ip, state->port_port);
        struct sockaddr_in data_addr;
        dataSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (dataSocket < 0)
        {
            perror("[ERROR] socket");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            fclose(file);
            return;
        }

        memset(&data_addr, 0, sizeof(data_addr));
        data_addr.sin_family = AF_INET;
        data_addr.sin_port = htons(state->port_port);
        inet_pton(AF_INET, state->port_ip, &data_addr.sin_addr);

        if (connect(dataSocket, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0)
        {
            perror("[ERROR] connect");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            fclose(file);
            close(dataSocket);
            return;
        }
    }
    else
    {
        send(clientSocket, "425 Use PORT or PASV first.\r\n", 29, 0);
        fclose(file);
        return;
    }

    state->is_transferring = 1;
    while ((bytes_read = recv(dataSocket, buffer, sizeof(buffer), 0)) > 0)
    {
        if (fwrite(buffer, 1, bytes_read, file) < bytes_read)
        {
            perror("[ERROR] fwrite");
            send(clientSocket, "552 Requested file action aborted. Exceeded storage allocation.\r\n", 67, 0);
            break;
        }
    }

    if (bytes_read < 0)
    {
        perror("[ERROR] recv");
        send(clientSocket, "426 Connection closed; transfer aborted.\r\n", 49, 0);
    }

    flock(file_fd, LOCK_UN);
    state->is_transferring = 0;
    state->transfer_offset = 0;

    fclose(file);
    close(dataSocket);

    char response[256];
    memset(response, 0, sizeof(response)); // 初始化整个缓冲区为0
    snprintf(response, sizeof(response), "226 Transfer complete.\r\n");

    send(clientSocket, response, strlen(response), 0);
    printf("[DEBUG] File transfer complete: %s\n", full_path);
}
// 实现 ftp_mkd 函数
void ftp_mkd(int clientSocket, FtpState *state, char *dirname)
{
    char full_path[MAXSIZE];
    char msg[MAXSIZE];

    if (dirname == NULL || strlen(dirname) == 0)
    {
        send(clientSocket, "550 Invalid directory name.\r\n", 29, 0);
        return;
    }

    // 构建完整路径
    snprintf(full_path, sizeof(full_path), "%s%s/%s", state->root_dir, state->current_dir, dirname);
    normalize_path(full_path);

    printf("[DEBUG] MKD command received. Full path to create directory: %s\n", full_path);

    // 创建目录
    if (mkdir(full_path, 0755) < 0)
    {
        perror("[ERROR] mkdir");
        sprintf(msg, "550 Failed to create directory.\r\n");
        send(clientSocket, msg, strlen(msg), 0);
        return;
    }

    // 发送成功消息
    sprintf(msg, "257 \"%s\" created.\r\n", dirname);
    send(clientSocket, msg, strlen(msg), 0);

    printf("[DEBUG] Directory created: %s\n", full_path);
}

// 修复后的 ftp_cwd 实现
void ftp_cwd(int clientSocket, FtpState *state, const char *dirname)
{
    char full_path[MAXSIZE];
    char msg[MAXSIZE];
    char temp_dir[MAXSIZE];

    // 检查 dirname 是否为空或无效
    if (dirname == NULL || strlen(dirname) == 0)
    {
        send(clientSocket, "550 Invalid directory name.\r\n", 29, 0);
        return;
    }

    // 处理 ".." 或其他特殊目录时需要进行路径更新
    if (strcmp(dirname, "..") == 0)
    {
        // 返回上一级目录
        strcpy(temp_dir, state->current_dir);
        char *last_slash = strrchr(temp_dir, '/');
        if (last_slash != NULL && last_slash != temp_dir)
        {
            *last_slash = '\0'; // 删除最后一个斜杠及其后面的部分
        }
        else
        {
            strcpy(temp_dir, "/"); // 如果已经在根目录，保持根目录
        }
    }
    else if (strcmp(dirname, ".") == 0)
    {
        // 如果是当前目录，不做任何改变
        strcpy(temp_dir, state->current_dir);
    }
    else
    {
        // 普通目录：拼接路径（确保格式正确）
        if (state->current_dir[strlen(state->current_dir) - 1] == '/')
        {
            snprintf(temp_dir, sizeof(temp_dir), "%s%s", state->current_dir, dirname);
        }
        else
        {
            snprintf(temp_dir, sizeof(temp_dir), "%s/%s", state->current_dir, dirname);
        }
    }

    // 构建完整的绝对路径：根目录 + temp_dir
    snprintf(full_path, sizeof(full_path), "%s%s", state->root_dir, temp_dir);
    normalize_path(full_path);

    printf("[DEBUG] CWD command received. Full path to change directory: %s\n", full_path);

    // 检查目录是否存在并切换目录（chdir 用于检查目录是否有效）
    if (chdir(full_path) < 0)
    {
        perror("[ERROR] chdir");
        sprintf(msg, "550 Failed to change directory.\r\n");
        send(clientSocket, msg, strlen(msg), 0);
        return;
    }

    // 更新当前工作目录
    snprintf(state->current_dir, sizeof(state->current_dir), "%s", temp_dir);
    normalize_path(state->current_dir);

    // 发送成功消息
    sprintf(msg, "250 Directory changed to \"%s\".\r\n", state->current_dir);
    send(clientSocket, msg, strlen(msg), 0);

    printf("[DEBUG] Directory changed: %s\n", state->current_dir);
}
// 实现 ftp_pwd 函数
void ftp_pwd(int clientSocket, FtpState *state)
{
    char msg[MAXSIZE];

    // 发送当前工作目录
    sprintf(msg, "257 \"%s\" is the current directory.\r\n", state->current_dir);
    send(clientSocket, msg, strlen(msg), 0);

    printf("[DEBUG] PWD command processed. Current directory: %s\n", state->current_dir);
}

// 实现 ftp_rmd 函数
void ftp_rmd(int clientSocket, FtpState *state, const char *dirname)
{
    char full_path[MAXSIZE];
    char msg[MAXSIZE];

    if (dirname == NULL || strlen(dirname) == 0)
    {
        send(clientSocket, "550 Invalid directory name.\r\n", 29, 0);
        return;
    }

    // 构建完整路径
    snprintf(full_path, sizeof(full_path), "%s%s/%s", state->root_dir, state->current_dir, dirname);
    normalize_path(full_path);

    printf("[DEBUG] Full path to remove directory: %s\n", full_path);

    // 检查目录是否存在
    if (access(full_path, F_OK) != 0)
    {
        perror("[ERROR] Directory does not exist");
        send(clientSocket, "550 Directory does not exist.\r\n", 31, 0);
        return;
    }

    // 加锁 防止多个客户端同时删除
    // 加锁（针对删除目录的操作）
    int dir_fd = open(full_path, O_RDONLY); // 打开目录
    if (dir_fd < 0)
    {
        perror("[ERROR] open");
        send(clientSocket, "550 Directory does not exist.\r\n", 31, 0);
        return;
    }
    if (flock(dir_fd, LOCK_EX) < 0) // 独占锁
    {
        perror("[ERROR] flock");
        close(dir_fd);
        send(clientSocket, "550 Failed to lock directory.\r\n", 32, 0);
        return;
    }

    // 使用 rmdir 删除目录
    if (rmdir(full_path) < 0)
    {
        perror("[ERROR] rmdir");
        send(clientSocket, "550 Failed to remove directory.\r\n", 33, 0);
        return;
    }

    // 解锁
    // 解锁
    flock(dir_fd, LOCK_UN);
    close(dir_fd);

    // 发送成功消息
    sprintf(msg, "250 Directory \"%s\" removed successfully.\r\n", dirname);
    send(clientSocket, msg, strlen(msg), 0);

    printf("[DEBUG] Directory removed successfully: %s\n", full_path);
}

// 实现list函数
// 实现 ftp_list 函数
void ftp_list(int clientSocket, FtpState *state, const char *parameter)
{
    char full_path[MAXSIZE];
    char msg[MAXSIZE];
    char data_buffer[MAXSIZE];
    int dataSocket;
    FILE *ls_fp;

    // 设置默认目录为当前目录
    if (parameter == NULL || strlen(parameter) == 0)
    {
        snprintf(full_path, sizeof(full_path), "%s%s", state->root_dir, state->current_dir);
    }
    else
    {
        // 处理带参数的 LIST 命令，例如：LIST subdir
        snprintf(full_path, sizeof(full_path), "%s%s/%s", state->root_dir, state->current_dir, parameter);
    }

    // 规范化路径（去除多余的斜杠）
    normalize_path(full_path);
    printf("[DEBUG] LIST command received. Full path to list: %s\n", full_path);

    // 检查目录是否存在
    if (access(full_path, F_OK) < 0)
    {
        perror("[ERROR] access");
        send(clientSocket, "550 Directory not found.\r\n", 26, 0);
        return;
    }

    // 先发送150响应
    send(clientSocket, "150 Here comes the directory listing.\r\n", 39, 0);

    // 建立数据连接（PORT 或 PASV 模式）
    dataSocket = setup_data_connection(clientSocket, state);
    if (dataSocket < 0)
    {
        send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
        return;
    }

    // 执行 ls -l 命令获取目录列表
    char cmd[MAXSIZE];
    snprintf(cmd, sizeof(cmd), "ls -l %s", full_path);
    ls_fp = popen(cmd, "r");
    if (ls_fp == NULL)
    {
        perror("[ERROR] popen");
        send(clientSocket, "451 Failed to list directory.\r\n", 31, 0);
        close(dataSocket);
        return;
    }

    // 通过数据连接发送目录列表
    while (fgets(data_buffer, sizeof(data_buffer), ls_fp) != NULL)
    {
        send(dataSocket, data_buffer, strlen(data_buffer), 0);
    }

    // 关闭目录列表文件指针
    pclose(ls_fp);

    // 关闭数据连接
    close(dataSocket);

    // 发送 226 响应，表示传输完成
    send(clientSocket, "226 Directory send OK.\r\n", 24, 0);
    printf("[DEBUG] Directory listing sent successfully for path: %s\n", full_path);
}

// 处理 REST 命令
void ftp_rest(int clientSocket, FtpState *state, const char *offset)
{
    printf("[DEBUG] REST command received with offset: %s\n", offset);

    // 将字符串转换为长整数，表示偏移量
    long file_offset = atol(offset);

    // 验证偏移量是否合理
    if (file_offset < 0)
    {
        send(clientSocket, "501 Invalid offset.\r\n", 22, 0);
        return;
    }

    // 保存偏移量到服务器状态中
    state->transfer_offset = file_offset;
    printf("[DEBUG] File transfer offset set to: %ld\n", file_offset);

    // 响应客户端，通知 REST 命令成功
    send(clientSocket, "350 Restart position accepted.\r\n", 33, 0);
}

// 实现 ftp_size 函数
void ftp_size(int clientSocket, FtpState *state, const char *filename)
{
    char full_path[MAXSIZE];
    char msg[MAXSIZE];

    // 构建完整的文件路径：基于 root_dir 和 current_dir
    if (strcmp(state->current_dir, "/") == 0)
    {
        snprintf(full_path, sizeof(full_path), "%s/%s", state->root_dir, filename);
    }
    else
    {
        snprintf(full_path, sizeof(full_path), "%s%s/%s", state->root_dir, state->current_dir, filename);
    }

    printf("[DEBUG] SIZE command received. Full path to file: %s\n", full_path);

    // 检查文件是否存在
    FILE *file = fopen(full_path, "rb"); // 打开文件，二进制模式
    if (file == NULL)
    {
        perror("[ERROR] fopen");
        send(clientSocket, "550 File not found.\r\n", 21, 0);
        return;
    }

    // 获取文件大小
    fseek(file, 0, SEEK_END);     // 将文件指针移到文件末尾
    long file_size = ftell(file); // 获取文件指针当前位置（文件大小）
    fclose(file);                 // 关闭文件

    // 发送文件大小给客户端，响应格式：213 文件大小
    snprintf(msg, sizeof(msg), "213 %ld\r\n", file_size);
    send(clientSocket, msg, strlen(msg), 0);

    printf("[DEBUG] File size sent: %ld bytes\n", file_size);
}
void ftp_retr_resume(int clientSocket, FtpState *state, char *filename)
{
    printf("[DEBUG] RETR resume command received with filename: %s\n", filename);
    FILE *file;
    char buffer[MAXSIZE];
    char full_path[MAXSIZE];
    int dataSocket;
    int bytes_read;

    // Construct the full file path based on root_dir and current_dir
    snprintf(full_path, sizeof(full_path), "%s%s/%s", state->root_dir, strcmp(state->current_dir, "/") == 0 ? "" : state->current_dir, filename);

    printf("[DEBUG] RETR resume command. Full path to retrieve: %s\n", full_path);

    // Check if file exists and is readable
    file = fopen(full_path, "rb"); // Open file in binary mode
    if (file == NULL)
    {
        perror("fopen");
        send(clientSocket, "550 File not found or access denied.\r\n", 37, 0);
        return;
    }

    // Check if data connection is active (PORT or PASV)
    if (state->data_connection_active == 0)
    {
        send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
        fclose(file);
        return;
    }

    send(clientSocket, "150 Opening BINARY mode data connection.\r\n", 42, 0);

    if (state->pasv_socket > 0)
    {
        printf("[DEBUG] Waiting for client to connect to PASV port: %d\n", state->pasv_port);
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        dataSocket = accept(state->pasv_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (dataSocket < 0)
        {
            perror("accept");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            fclose(file);
            return;
        }
        close(state->pasv_socket);
        state->pasv_socket = -1;
    }
    else
    {
        printf("[DEBUG] Initiating active mode connection to: %s:%d\n", state->port_ip, state->port_port);
        struct sockaddr_in data_addr;
        memset(&data_addr, 0, sizeof(data_addr));
        data_addr.sin_family = AF_INET;
        data_addr.sin_port = htons(state->port_port);
        inet_pton(AF_INET, state->port_ip, &data_addr.sin_addr);

        dataSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (dataSocket < 0)
        {
            perror("socket");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            fclose(file);
            return;
        }

        if (connect(dataSocket, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0)
        {
            perror("connect");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            close(dataSocket);
            fclose(file);
            return;
        }
    }

    if (state->transfer_offset > 0)
    {
        fseek(file, state->transfer_offset, SEEK_SET);
        printf("[DEBUG] Resuming transfer from offset: %lld\n", (long long)state->transfer_offset);
    }

    state->is_transferring = 1;
    printf("[DEBUG] Starting file transfer...\n");

    while ((bytes_read = fread(buffer, 1, MAXSIZE, file)) > 0)
    {
        int total_sent = 0;
        while (total_sent < bytes_read)
        {
            int sent = send(dataSocket, buffer + total_sent, bytes_read - total_sent, 0);
            if (sent < 0)
            {
                perror("send");
                send(clientSocket, "426 Connection closed; transfer aborted.\r\n", 43, 0);
                fclose(file);
                close(dataSocket);
                state->is_transferring = 0;
                return;
            }
            total_sent += sent;
        }
    }
    printf("[DEBUG] File transfer complete.\n");

    state->is_transferring = 0;
    fclose(file);
    shutdown(dataSocket, SHUT_WR);
    close(dataSocket);

    send(clientSocket, "226 Transfer complete.\r\n", 24, 0);
    printf("[DEBUG] Data connection closed.\n");
}
void ftp_stor_resume(int clientSocket, FtpState *state, char *filename)
{
    char full_path[MAXSIZE];
    FILE *file;
    char buffer[MAXSIZE];
    int bytes_read;
    int dataSocket;

    snprintf(full_path, sizeof(full_path), "%s%s/%s", state->root_dir, strcmp(state->current_dir, "/") == 0 ? "" : state->current_dir, filename);

    printf("[DEBUG] STOR resume command received. Full path to save file: %s\n", full_path);

    file = fopen(full_path, "rb+");
    if (file == NULL)
    {
        perror("[ERROR] fopen");
        send(clientSocket, "550 Failed to open file.\r\n", 27, 0);
        return;
    }

    fseek(file, state->transfer_offset, SEEK_SET);
    printf("[DEBUG] Resuming upload at offset: %lld\n", (long long)state->transfer_offset);

    int file_fd = fileno(file);
    if (flock(file_fd, LOCK_EX | LOCK_NB) != 0)
    {
        perror("[ERROR] flock");
        send(clientSocket, "550 File is currently being written by another client.\r\n", 55, 0);
        fclose(file);
        return;
    }

    send(clientSocket, "150 Opening BINARY mode data connection.\r\n", 42, 0);

    if (state->pasv_socket > 0)
    {
        printf("[DEBUG] Waiting for client to connect to PASV port: %d\n", state->pasv_port);
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        dataSocket = accept(state->pasv_socket, (struct sockaddr *)&client_addr, &addr_len);
        if (dataSocket < 0)
        {
            perror("[ERROR] accept");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            fclose(file);
            return;
        }
        close(state->pasv_socket);
        state->pasv_socket = -1;
    }
    else if (state->port_port > 0)
    {
        printf("[DEBUG] Initiating active mode connection to: %s:%d\n", state->port_ip, state->port_port);
        struct sockaddr_in data_addr;
        dataSocket = socket(AF_INET, SOCK_STREAM, 0);
        if (dataSocket < 0)
        {
            perror("[ERROR] socket");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            fclose(file);
            return;
        }

        memset(&data_addr, 0, sizeof(data_addr));
        data_addr.sin_family = AF_INET;
        data_addr.sin_port = htons(state->port_port);
        inet_pton(AF_INET, state->port_ip, &data_addr.sin_addr);

        if (connect(dataSocket, (struct sockaddr *)&data_addr, sizeof(data_addr)) < 0)
        {
            perror("[ERROR] connect");
            send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
            fclose(file);
            close(dataSocket);
            return;
        }
    }
    else
    {
        send(clientSocket, "425 Use PORT or PASV first.\r\n", 29, 0);
        fclose(file);
        return;
    }

    state->is_transferring = 1;
    while ((bytes_read = recv(dataSocket, buffer, sizeof(buffer), 0)) > 0)
    {
        if (fwrite(buffer, 1, bytes_read, file) < bytes_read)
        {
            perror("[ERROR] fwrite");
            send(clientSocket, "552 Requested file action aborted. Exceeded storage allocation.\r\n", 67, 0);
            break;
        }
    }

    if (bytes_read < 0)
    {
        perror("[ERROR] recv");
        send(clientSocket, "426 Connection closed; transfer aborted.\r\n", 49, 0);
    }

    flock(file_fd, LOCK_UN);
    state->is_transferring = 0;
    state->transfer_offset = 0;

    fclose(file);
    close(dataSocket);
    send(clientSocket, "226 Transfer complete.\r\n", 24, 0);
    printf("[DEBUG] File transfer complete: %s\n", full_path);
}
