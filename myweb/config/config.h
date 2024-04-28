#ifndef CONFIG_H
#define CONFIG_H

#include "../webserver/webserver.h"

class Config{
public:
    Config();
    ~Config();

    void parse(int argc, char*argv[]);
    
    int m_port;//端口号
    int m_logwrite;//写日志的方式
    int m_TRIGMode;//触发组合模式
    int m_listenTRIGMode;// listenfd 触发模式
    int m_connTRIGMode;// connfd 触发模式
    int m_OPT_LINGER;// 关闭连接
    int m_sqlnum;//数据库连接池的数量
    int m_threadnum;// 线程池内的线程数量
    int m_close_log;// 是否关闭日志
    int m_actor_model;// 并发模型的选择
private:

};

#endif
