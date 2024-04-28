#ifndef LOG_H
#define LOG_H

#include <stdio.h>
#include <iostream>
#include <string>
#include <stdarg.h>
#include <pthread.h>

#include "block_queue.h"

using namespace std;

class Log{
public:
    //单例模式
    //获取 Log 类的单例对象
    static Log* get_instance(){
        static Log instance;
        return &instance;
    }
    //在单独的线程中异步写入日志消息    回调函数
    static void* flush_log_thread(void* args){
        Log::get_instance()->async_write_log();
    }

    // 参数为日志文件、日志缓冲区大小、最大行数以及最长日志条队列
    bool init(const char* file_name,int close_log,int log_buf_size = 8192, int split_lines = 5000000, int max_queue_size = 0);
    // 一个整数参数 level 用于指定日志级别，以及一个格式化字符串参数 format 用于指定日志消息的格式
    void write_log(int level, const char *format, ...);
    //刷新日志缓冲区
    void flush();
private:
    //将构造和析构私有化
    Log();
    virtual ~Log();
    
    //异步写入日志的函数
    void *async_write_log()
    {
        string single_log;
        //从阻塞队列中取出一个日志string，写入文件
        while (m_log_queue->pop(single_log))
        {
            //加锁操作，保证线程同步
            m_mutex.lock();
            fputs(single_log.c_str(), m_fp);
            m_mutex.unlock();
        }
    }

private:
    char dir_name[128]; //路径名
    char log_name[128]; //log文件名
    int m_split_lines;  //日志最大行数
    int m_log_buf_size; //日志缓冲区大小
    long long m_count;  //日志行数记录
    int m_today;        //因为按天分类,记录当前时间是那一天
    FILE *m_fp;         //打开log的文件指针
    char *m_buf;
    block_queue<std::string> *m_log_queue; //阻塞队列
    bool m_is_async;                  //是否同步标志位
    locker m_mutex;
    int m_close_log; //关闭日志
};

//日志级别
#define LOG_DEBUG(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(0, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_INFO(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(1, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_WARN(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(2, format, ##__VA_ARGS__); Log::get_instance()->flush();}
#define LOG_ERROR(format, ...) if(0 == m_close_log) {Log::get_instance()->write_log(3, format, ##__VA_ARGS__); Log::get_instance()->flush();}

#endif