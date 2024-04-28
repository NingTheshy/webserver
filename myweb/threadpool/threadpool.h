#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <cstdio>
#include <list>
#include <pthread.h>
#include <exception>

#include "../lock/locker.h"
#include "../mysql-connection/mysql_connection_pool.h"

using namespace std;

template<typename T>
class threadpool{
public:
    threadpool(int model, connection_pool *connpool, int thread_num = 8, int max_questsize = 10000);
    ~threadpool();

    //向请求队列中添加任务
    bool append(T* request);
    bool append_p(T* request,int state);
private:
    void run();//启动线程池
    static void* worker(void* args);//回调函数    工作线程运行的函数，它不断从工作队列中取出任务并执行之
private:
    pthread_t* m_threads;//线程池数组，其大小为 m_thread_num
    locker lock;
    list<T*>m_queue;//请求队列
    int m_max_queuesize;//请求队列的最大数量
    sem m_sem;
    bool m_stop;//是否结束线程
    int m_thread_num;//线程数量
    connection_pool* m_connpoll;//数据库
    int m_model;//模型切换
};


template<typename T>
threadpool<T>::threadpool(int model, connection_pool *connpool, int thread_num, int max_questsize) : m_thread_num(thread_num), 
                         m_max_queuesize(max_questsize), m_model(model), m_connpoll(connpool), m_stop(false), m_threads(NULL) {
    if(thread_num <= 0 || max_questsize <= 0) {
        throw exception();
    }
    // 创建线程数组
    m_threads = new pthread_t[m_thread_num];
    if(!m_threads){
        throw exception();
    }

    // 创建thread_number 个线程，并将他们设置为脱离线程
    for(int i = 0; i < thread_num; i++){
        if(pthread_create(m_threads + i, NULL, worker, this) != 0){
            delete[] m_threads;
            throw exception();
        }
        if(pthread_detach(m_threads[i])){
            delete[] m_threads;
            throw exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool(){
    delete[] m_threads;
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T *request){
    lock.lock();

    if(m_queue.size() >= m_max_queuesize){
        lock.unlock();
        return false;
    }
    m_queue.push_back(request);

    lock.unlock();
    m_sem.post();
    return true;
}

template <typename T>
bool threadpool<T>::append_p(T *request, int state){
    lock.lock();

    if(m_queue.size() >= m_max_queuesize){
        lock.unlock();
        return false;
    }
    request->m_state = state;
    m_queue.push_back(request);

    lock.unlock();
    m_sem.post();
    return true;
}

template <typename T>
void threadpool<T>::run(){
    while(!m_stop){
        m_sem.wait();
        lock.lock();

        if(m_queue.empty()){
            lock.unlock();
            continue;
        }
        
        T* request = m_queue.front();
        m_queue.pop_front();
        lock.unlock();
        if(!request){
            continue;;
        }
        if(m_model == 1){
            //读
            if(request->m_state == 0){
                if(request->read_once()){
                    request->improv = 1;
                    connectionRAII mysqlcon(&request->mysql, m_connpoll);
                    request->process();
                }else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }else{
                if(request->write()){
                    request->improv = 1;
                }else{
                    request->improv = 1;
                    request->timer_flag = 1;
                }
            }
        }else{
            connectionRAII mysql(&request->mysql, m_connpoll);
            request->process();
        }
    }
}

template <typename T>
void *threadpool<T>::worker(void *args){
    threadpool* pool = (threadpool*)args;
    pool->run();
    return pool;
}


#endif