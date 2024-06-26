#include <iostream>
#include <string>

#include "./config/config.h"

using namespace std;

int main(int argc, char *argv[]) {
    //需要修改的数据库信息,登录名,密码,库名
    string user = "root";
    string passwd = "123";
    string databasename = "webserver";

    //命令行解析
    Config config;
    config.parse(argc, argv);

    WebServer server;

    //初始化
    server.init(config.m_port, user, passwd, databasename, config.m_logwrite, config.m_OPT_LINGER, config.m_TRIGMode,  config.m_sqlnum,  
                config.m_threadnum, config.m_close_log, config.m_actor_model);
    
    //日志
    server.log_write();

    //数据库
    server.sql_pool();

    //线程池
    server.thread_pool();

    //触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}
