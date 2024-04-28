#include "../log/log.h"
#include <stdio.h>
#include <string.h>
#include <string>
#include <stdarg.h>
#include <sys/time.h>


Log::Log(){
    m_count = 0;
    m_is_async = false;
}

Log::~Log(){
    //在释放日志对象时关闭文件句柄，确保在程序退出或日志对象销毁时关闭日志文件
    if (m_fp != NULL){
        fclose(m_fp);
    }
}

bool Log::init(const char *file_name, int close_log, int log_buf_size, int split_lines, int max_queue_size){
    // 如果设置了max_queue_size,则设置为异步
    if (max_queue_size >= 1){
        m_is_async = true;
        m_log_queue = new block_queue<std::string>(max_queue_size);
        // 创建一个新线程用于异步写日志
        pthread_t tid;
        // flush_log_thread为回调函数,这里表示创建线程异步写日志
        pthread_create(&tid, NULL, flush_log_thread, NULL);
    }
    
    this->m_close_log = close_log;
    this->m_log_buf_size = log_buf_size;
    this->m_buf = new char[m_log_buf_size];
    //确保日志缓冲区 m_buf 在使用之前被正确清空
    memset(m_buf, '\0', m_log_buf_size);
    this->m_split_lines = split_lines;

    // 获取当前时间，用于日志文件名中的日期部分
    time_t t = time(NULL);
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;

    // 构造日志文件的完整路径和文件名   命名规则为 年_月_日_文件名
    const char *p = strrchr(file_name, '/');
    char log_full_name[256] = {0};

    if (p == NULL){
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, file_name);
    }else{
        strcpy(log_name, p + 1);
        strncpy(dir_name, file_name, p - file_name + 1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", dir_name, my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday, log_name);
    }

    m_today = my_tm.tm_mday;
    
    // 打开日志文件     如果打开失败则返回 false    
    // 使用 fopen() 函数打开日志文件，模式为 "a"，表示以追加（append）模式打开文件，即如果文件不存在，则创建文件；如果文件存在，则在文件末尾追加写入
    m_fp = fopen(log_full_name, "a");
    if (m_fp == NULL){
        //打开失败      意味着初始化函数失败，日志系统无法正常工作
        return false;
    }

    return true;
}

void Log::write_log(int level, const char *format, ...){
    // 获取当前时间，并根据日志级别生成日志前缀
    struct timeval now = {0, 0};
    gettimeofday(&now, NULL);
    time_t t = now.tv_sec;
    struct tm *sys_tm = localtime(&t);
    struct tm my_tm = *sys_tm;
    char s[16] = {0};
    // 日志分级
    switch (level){
    case 0:
        strcpy(s, "[debug]:");
        break;
    case 1:
        strcpy(s, "[info]:");
        break;
    case 2:
        strcpy(s, "[warn]:");
        break;
    case 3:
        strcpy(s, "[erro]:");
        break;
    default:
        strcpy(s, "[info]:");
        break;
    }

    m_mutex.lock();

    //更新现有行数
    m_count++;

    //日志不是今天或写入的日志行数是最大行的倍数
    //m_split_lines为最大行数
    if (m_today != my_tm.tm_mday || m_count % m_split_lines == 0) {
        
        char new_log[256] = {0};
        fflush(m_fp);
        fclose(m_fp);
        char tail[16] = {0};
       
        // 格式化日志名中的时间部分
        snprintf(tail, 16, "%d_%02d_%02d_", my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday);

        // 如果是时间不是今天,则创建今天的日志，更新m_today和m_count
        if (m_today != my_tm.tm_mday){
            snprintf(new_log, 255, "%s%s%s", dir_name, tail, log_name);
            m_today = my_tm.tm_mday;
            m_count = 0;
        }else{
            // 超过了最大行，在之前的日志名基础上加后缀, m_count/m_split_lines
            snprintf(new_log, 255, "%s%s%s.%lld", dir_name, tail, log_name, m_count / m_split_lines);
        }
        m_fp = fopen(new_log, "a");
    }
 
    m_mutex.unlock();

    va_list valst;
    // 将传入的format参数赋值给valst，便于格式化输出
    va_start(valst, format);

    std::string log_str;
    m_mutex.lock();

    // 写入内容格式：时间 + 内容
    // 时间格式化，snprintf成功返回写字符的总数，其中不包括结尾的null字符
    int n = snprintf(m_buf, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",my_tm.tm_year + 1900, my_tm.tm_mon + 1, my_tm.tm_mday,
                     my_tm.tm_hour, my_tm.tm_min, my_tm.tm_sec, now.tv_usec, s);
    
    // 内容格式化，用于向字符串中打印数据、数据格式用户自定义，返回写入到字符数组str中的字符个数(不包含终止符)
    int m = vsnprintf(m_buf + n, m_log_buf_size - n - 1, format, valst);
    m_buf[n + m] = '\n';
    m_buf[n + m + 1] = '\0';
    log_str = m_buf;

    m_mutex.unlock();

    // 将日志消息写入日志文件或日志队列     如果开启了异步写日志功能，并且消息队列未满，则将日志消息压入消息队列；否则直接写入日志文件
    if (m_is_async && !m_log_queue->full()){
        m_log_queue->push(log_str);
    }else{
        m_mutex.lock();
        fputs(log_str.c_str(), m_fp);
        m_mutex.unlock();
    }

    // 一个宏，用于结束使用 va_start() 宏开始的可变参数列表的访问   确保了对可变参数列表的正确处理，避免了潜在的内存泄漏或未定义行为
    va_end(valst);
}

//强制刷新日志文件缓冲区，确保日志内容被写入文件
void Log::flush(void){
    m_mutex.lock();
    //强制刷新写入流缓冲区
    fflush(m_fp);
    m_mutex.unlock();
}