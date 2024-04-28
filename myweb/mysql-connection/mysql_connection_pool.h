#ifndef MYSQL_CONNECTION_POOL_H
#define MYSQL_CONNECTION_POOL_H

#include "../lock/locker.h"
#include "../log/log.h"
#include <mysql/mysql.h>
#include <list>
#include <iostream>
#include <string>

using namespace std;

class connection_pool{
public:
    //单例模式
    static connection_pool* GetInstance();
    //初始化
    void init(string url, string User, string PassWord, string DataBaseName, int Port, int MaxConn, int close_log);

    //获取数据库连接
    MYSQL* getconnection();
    // 当有请求时，从数据库连接池中返回一个可用连接，更新使用和空闲连接数
    int GetFreeConn();
    // 释放当前使用的连接
    bool ReleaseConnection(MYSQL* conn);
    //销毁所有连接
    void DestroyPool();

    string m_url;//主机地址
    string m_port;//端口
    string m_user;//用户名
    string m_password;//密码
    string m_databasename;//数据库名
    int m_close_log;//日志开关
private:
    connection_pool();
    ~connection_pool();

    locker lock;
    int m_MaxConn;  //最大连接数
	int m_CurConn;  //当前已使用的连接数
	int m_FreeConn; //当前空闲的连接数
    sem m_sem;
    list<MYSQL*>connList;//连接池
};

//RAII 是一种资源管理的编程范式，通过在对象的构造函数中获取资源，在析构函数中释放资源，来确保资源的正确释放。
//在这里，connectionRAII 类的对象在构造时获取一个数据库连接，在析构时释放该连接，从而保证连接的安全释放     
//将数据库连接的获取与释放通过RAII机制封装，避免手动释放。
class connectionRAII{
public:
    // 在获取连接时，通过有参构造对传入的参数进行修改。其中数据库连接本身是指针类型，所以参数需要通过双指针才能对其进行修改
    // 双指针对MYSQL *con修改
    connectionRAII(MYSQL** SQL, connection_pool* connpool);
    ~connectionRAII();
private:
    MYSQL *conRAII;
	connection_pool *poolRAII;
};

#endif