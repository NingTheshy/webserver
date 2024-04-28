#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>
#include <iostream>

#include "../threadpool/threadpool.h"
#include "../http/http_conn.h"

using namespace std;

const int MAX_FD = 65536;// 最大文件描述符
const int MAX_EVENT_NUMBER = 10000;// 最大事件数
const int TIMESLOT = 5;// 最小超时单位

class WebServer{
public:
    WebServer();
    ~WebServer();

    //初始化函数，用于设置服务器的参数，如端口号、数据库信息、日志选项等
    void init(int port , string user, string passWord, string databaseName,
              int log_write , int opt_linger, int trigmode, int sql_num, int thread_num, int close_log, int model);
    void thread_pool();//创建并初始化线程池，用于处理客户端请求
    void sql_pool();//创建并初始化数据库连接池，用于执行数据库操作
    void log_write();//写日志函数，用于记录服务器运行状态和客户端请求信息

    void trig_mode();//设置服务器的事件触发模式，包括监听套接字触发模式（LISTENTrigmode）和连接套接字触发模式（CONNTrigmode）等
    void eventListen();// 监听事件函数，用于监听客户端连接请求
    void eventLoop();//事件循环函数，用于处理发生在服务器上的各种事件，例如客户端连接、数据读取、数据写入等

    void timer(int connfd, struct sockaddr_in client_address);//设置定时器函数，用于为客户端连接设置超时时间
    void adjust_timer(util_timer *timer);//调整定时器函数，用于调整客户端连接的超时时间
    void deal_timer(util_timer *timer, int sockfd);//处理定时器函数，用于处理超时的客户端连接

    bool dealclientdata();//处理客户端数据函数，用于处理客户端发送的数据
    bool dealwithsignal(bool& timeout, bool& stop_server);//处理信号函数，用于处理服务器收到的信号，例如超时信号或停止服务器信号
    void dealwithread(int sockfd);//处理读事件函数，用于处理客户端连接上的数据读取事件
    void dealwithwrite(int sockfd);//处理写事件函数，用于处理客户端连接上的数据写入事件

public:
    //基础  定义了一些服务器基本参数，如端口号、根目录、日志选项
    int m_port;
    char* m_root;
    int m_log_write;
    int m_close_log;
    int m_model;

    //定义了管道和 epoll 文件描述符，以及 HTTP 连接对象
    int m_pipefd[2];
    int m_epollfd;
    http_conn *users;

    //与数据库相关的
    connection_pool *m_connpool;
    string m_user;// 登陆数据库用户名
    string m_passWord;// 登陆数据库密码
    string m_databaseName;// 使用数据库名
    int m_sql_num;

    //与线程池相关的
    threadpool<http_conn>*m_pool;
    int m_thread_num;

    //与 epoll_event 相关的
    epoll_event events[MAX_EVENT_NUMBER];//存储事件的 epoll_event 数组

    //定义监听文件描述符和一些触发模式相关的参数
    int m_listenfd;
    int m_OPT_LINGER;
    int m_TRIGMode;
    int m_LISTENTrigmode;// 处理监听    0 为 LT ，1 为 ET
    int m_CONNTrigmode;

    //与定时器相关的
    client_data *users_timer;
    Utils utils;
};

#endif