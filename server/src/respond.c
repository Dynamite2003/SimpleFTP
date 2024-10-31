#include "server.h"

void respond_to_client(char *input_msg, int clientSocket, char *root_dir, FtpState *current_State)
{
    // 接受客户端命令，分离命令和参数
    char command[MAXSIZE];
    char arg[MAXSIZE] = {0}; // 初始化参数为空字符串，防止未传递参数时访问非法内存
    sscanf(input_msg, "%s %s", command, arg);

    printf("[DEBUG] Command received: %s, Argument: %s\n", command, arg);

    // 如果正在传输文件，只允许处理 ABOR 和 QUIT 命令
    if (current_State->is_transferring)
    {
        if (strcmp(command, "ABOR") == 0)
        {
            ftp_abor(clientSocket, current_State);
        }
        else if (strcmp(command, "QUIT") == 0)
        {
            ftp_quit(clientSocket, current_State);
        }
        else
        {
            // 忽略其他命令
            send(clientSocket, "425 Unable to process command during transfer.\r\n", 48, 0);
            printf("[DEBUG] Ignored command during transfer: %s\n", command);
        }
        return;
    }

    // 处理客户端命令，按照服务器状态
    switch (current_State->state)
    {
    case STATE_WAITING_USER:
        if (strcmp(command, "USER") == 0)
        {
            ftp_user(clientSocket, current_State, arg);
        }
        else
        {
            send(clientSocket, "530 Please login with USER and PASS.\r\n", 38, 0);
            printf("[DEBUG] Invalid command in STATE_WAITING_USER: %s\n", command);
        }
        break;

    case STATE_WAITING_PASS:
        if (strcmp(command, "PASS") == 0)
        {
            ftp_pass(clientSocket, current_State, arg);
        }
        else
        {
            send(clientSocket, "530 Please login with USER and PASS.\r\n", 38, 0);
            printf("[DEBUG] Invalid command in STATE_WAITING_PASS: %s\n", command);
        }
        break;

    case STATE_CLIENT_LOGIN:
        if (strcmp(command, "SYST") == 0)
        {
            send(clientSocket, "215 UNIX Type: L8\r\n", 19, 0);
        }
        else if (strcmp(command, "TYPE") == 0)
        {
            if (strcmp(arg, "I") != 0)
            {
                char *msg = "504 Command not implemented for that parameter.\r\n";
                send(clientSocket, msg, strlen(msg), 0);
            }
            else
            {
                char *msg = "200 Type set to I.\r\n";
                send(clientSocket, msg, strlen(msg), 0);
            }
        }
        else if (strcmp(command, "QUIT") == 0)
        {
            ftp_quit(clientSocket, current_State);
        }
        else if (strcmp(command, "ABOR") == 0)
        {
            ftp_abor(clientSocket, current_State);
        }
        else if (strcmp(command, "PORT") == 0)
        {
            ftp_port(clientSocket, current_State, arg);
        }
        else if (strcmp(command, "PASV") == 0)
        {
            ftp_pasv(clientSocket, current_State);
        }
        else if (strcmp(command, "RETR") == 0)
        {
            // 检查数据连接是否已激活
            if (current_State->data_connection_active == 0)
            {
                send(clientSocket, "425 Can't open data connection.\r\n", 33, 0);
                printf("[DEBUG] RETR command failed: Data connection not established.\n");
            }
            else
            {
                // 检查是否包含 -resume 参数
                if (strstr(input_msg, "-resume") != NULL)
                {
                    // 调用 ftp_retr_resume 函数继续传输
                    ftp_retr_resume(clientSocket, current_State, arg);
                }
                else
                {
                    // 调用 ftp_retr 函数进行文件传输
                    ftp_retr(clientSocket, current_State, arg);
                }
            }
        }
        else if (strcmp(command, "STOR") == 0)
        {
            // 检查是否有活动的数据连接
            if (current_State->data_connection_active)
            {
                // 检查是否包含 -resume 参数
                if (strstr(input_msg, "-resume") != NULL)
                {
                    // 调用 ftp_stor_resume 函数继续上传
                    ftp_stor_resume(clientSocket, current_State, arg);
                }
                else
                {
                    // 处理 STOR 命令
                    ftp_stor(clientSocket, current_State, arg);
                }
                current_State->data_connection_active = 0; // 重置数据连接状态
            }
            else
            {
                // 没有有效的数据连接
                send(clientSocket, "425 Use PORT or PASV first.\r\n", 29, 0);
            }
        }
        else if (strcmp(command, "MKD") == 0)
        {
            ftp_mkd(clientSocket, current_State, arg);
        }
        else if (strcmp(command, "CWD") == 0)
        {
            ftp_cwd(clientSocket, current_State, arg);
        }
        else if (strcmp(command, "PWD") == 0)
        {
            ftp_pwd(clientSocket, current_State);
        }
        else if (strcmp(command, "LIST") == 0)
        {
            ftp_list(clientSocket, current_State, arg);
        }
        else if (strcmp(command, "RMD") == 0)
        {
            ftp_rmd(clientSocket, current_State, arg);
        }
        else if (strcmp(command, "REST") == 0)
        {
            // 将偏移量保存到服务器状态中
            off_t offset = atoll(arg); // 转换偏移量参数
            current_State->transfer_offset = offset;

            char msg[MAXSIZE];
            sprintf(msg, "350 Restarting at %lld. Send STOR or RETR to resume transfer.\r\n", (long long)offset);
            send(clientSocket, msg, strlen(msg), 0);
            printf("[DEBUG] REST command received. Set transfer offset to %lld\n", (long long)offset);
        }
        else if (strcmp(command, "SIZE") == 0)
        {
            ftp_size(clientSocket, current_State, arg);
        }
        else
        {
            char msg[MAXSIZE];
            sprintf(msg, "502 Command not implemented: %s\r\n", command);
            send(clientSocket, msg, strlen(msg), 0);
            printf("[DEBUG] Unsupported command received: %s\n", command);
        }

        break;

    default:
        printf("[DEBUG] Invalid state: %d\n", current_State->state);
        break;
    }
}
