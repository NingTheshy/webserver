#include "config.h"

Config::Config() {
    //端口号
    this->m_port = 3000;

    //日志写入方式，默认同步
    this->m_logwrite = 0;

    //触发组合模式,默认listenfd LT + connfd LT
    this->m_TRIGMode = 0;

    //listenfd触发模式，默认LT
    this->m_listenTRIGMode = 0;

    //connfd触发模式，默认LT
    this->m_connTRIGMode = 0;

    //关闭链接，默认不使用
    this->m_OPT_LINGER = 0;

    //数据库连接池数量,默认8
    this->m_sqlnum = 8;

    //线程池内的线程数量,默认8
    this->m_threadnum = 8;

    //关闭日志,默认不关闭
    this->m_close_log = 0;

    //并发模型,默认是proactor
    this->m_actor_model = 0;
}

Config::~Config() {
}

void Config::parse(int argc, char*argv[]){
    int opt;
    const char *str = "p:l:m:o:s:t:c:a:";
    while ((opt = getopt(argc, argv, str)) != -1){
        switch (opt){
        case 'p':{
            this->m_port = atoi(optarg);
            break;
        }
        case 'l':{
            this->m_logwrite = atoi(optarg);
            break;
        }
        case 'm':{
            this->m_TRIGMode = atoi(optarg);
            break;
        }
        case 'o':{
            this->m_OPT_LINGER = atoi(optarg);
            break;
        }
        case 's':{
            this->m_sqlnum = atoi(optarg);
            break;
        }
        case 't':{
            this->m_threadnum = atoi(optarg);
            break;
        }
        case 'c':{
            this->m_close_log = atoi(optarg);
            break;
        }
        case 'a':{
            this->m_actor_model = atoi(optarg);
            break;
        }
        default:
            break;
        }
    }
}