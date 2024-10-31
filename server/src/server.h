#ifndef SERVER_H
#define SERVER_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <unistd.h>
// 加锁的头文件
#include <sys/file.h> // flock 头文件
#define MAXSIZE 1024
// 定义宏 表示当前服务器的状态
#define STATE_WAITING_USER 0
#define STATE_WAITING_PASS 1
#define STATE_CLIENT_LOGIN 2
#define USER_DB_FILE "/mnt/d/USER_PASS.txt"
// 定义一个结构体，用于保存当前服务器的状态
typedef struct
{
    int state;              // 当前服务器状态（等待用户、等待密码、已登录等）
    char username[MAXSIZE]; // 存储用户名
    char password[MAXSIZE]; // 存储密码

    // FTP 数据连接状态
    char port_ip[50];           // 保存PORT命令指定的客户端IP地址
    int port_port;              // 保存PORT命令指定的客户端端口号
    int pasv_socket;            // 被动模式的数据传输监听套接字（因为使用本机IP地址所以不需要IP地址）
    int pasv_port;              // 被动模式的监听端口号
    int data_connection_active; // 标识是否已设置了PORT或PASV命令（1为设置，0为未设置）

    // 文件操作状态
    char root_dir[MAXSIZE];    // 服务器的根目录（文件操作的起始路径）
    char current_dir[MAXSIZE]; // 当前工作目录（相对于root_dir的路径）

    int is_transferring; // 标志：是否正在进行文件传输
    // 记录文件传输的偏移量 支持断点重传
    long long int transfer_offset;
} FtpState;

void respond_to_client(char *input_msg, int clientSocket, char *root_dir, FtpState *current_state);

void ftp_quit(int clientSocket, FtpState *state);

void ftp_abor(int clientSocket, FtpState *state);

void ftp_port(int clientSocket, FtpState *state, char *arg);

void ftp_pasv(int clientSocket, FtpState *state);

void ftp_retr(int clientSocket, FtpState *state, const char *filename);

void ftp_stor(int clientSocket, FtpState *state, char *filename);

void ftp_mkd(int clientSocket, FtpState *state, char *dirname);

void ftp_cwd(int clientSocket, FtpState *state, const char *dirname);

void ftp_pwd(int clientSocket, FtpState *state);

void ftp_list(int clientSocket, FtpState *state, const char *parameter);

void ftp_rmd(int clientSocket, FtpState *state, const char *dirname);

void ftp_rest(int clientSocket, FtpState *state, const char *offset);

void ftp_size(int clientSocket, FtpState *state, const char *filename);

void ftp_retr_resume(int clientSocket, FtpState *state, char *filename);

void ftp_stor_resume(int clientSocket, FtpState *state, char *filename);
#endif