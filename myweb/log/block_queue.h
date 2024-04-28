//利用循环数组实现的阻塞队列    线程安全

#ifndef BLOCK_QUEUE_H
#define BLOCK_QUEUE_H

#include <iostream>
#include <stdlib.h>
#include <pthread.h>
#include <sys/time.h>

#include"../lock/locker.h"

template <class T>
class block_queue{
public:
    block_queue(int max_size = 1000){
        if(max_size <= 0){
            exit(-1);
        }
        this->m_max_size = max_size;
        this->m_array = new T[max_size];//动态分配队列的存储空间
        this->m_size = 0;
        this->m_front = -1;
        this->m_back = -1;
    }
    ~block_queue(){
        m_mutex.lock();
        if (m_array != NULL){
            delete []m_array;
        }
        m_mutex.unlock();
    }

    void clear(){
        //加锁保证线程安全
        m_mutex.lock();
        m_size = 0;
        m_front = -1;
        m_back = -1;
        m_mutex.unlock();
    }
    //返回队列队头元素
    bool front(T &value){
        m_mutex.lock();
        if(m_size == 0){
            //此处一定要解锁，否则会造成死锁现象
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_front];
        m_mutex.unlock();
        return true;
    }
    //返回队列队尾元素
    bool back(T &value){
        m_mutex.lock();
        if (m_size == 0){
            m_mutex.unlock();
            return false;
        }
        value = m_array[m_back];
        m_mutex.unlock();
        return true;
    }
    //判断队列是否已满
    bool full(){
        m_mutex.lock();
        if (m_size >= m_max_size){
            //此处一定要解锁，否则会造成死锁现象
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    bool empty(){
        m_mutex.lock();
        if(m_size == 0){
            m_mutex.unlock();
            return true;
        }
        m_mutex.unlock();
        return false;
    }
    int size(){
        m_mutex.lock();
        int res = m_size;
        m_mutex.unlock();
        return res;
    }
    int max_size(){
        m_mutex.lock();
        int res = m_max_size;
        m_mutex.unlock();
        return res;
    }
    //往队列中添加元素，需要将所有使用队列的线程先唤醒
    //当有元素push进队列,相当于生产者生产了一个元素
    //若当前没有线程等待条件变量,则唤醒无意义
    bool push(const T &item){
        m_mutex.lock();
        if(this->size() >= m_max_size){
            //如果队列已满，就通知所有等待的线程队列已满（唤醒）
            m_cond.broadcast();
            m_mutex.unlock();
            return false;
        }

        m_back = (m_back + 1) % m_max_size;
        m_array[m_back] = item;
        m_size++;

        //通知所有等待的线程队列中有新元素加入
        m_cond.broadcast();
        m_mutex.unlock();
        return true;
    }
    //pop时,如果当前队列没有元素,将会等待条件变量
    bool pop(T &item){
        m_mutex.lock();
        //队列为空时，当前线程一直等待，直到队列中有新的元素加入
        while(this->size() <= 0){
            if(!m_cond.wait(m_mutex.get())){
                //在等待期间，互斥锁会被释放，以允许其他线程对队列进行操作
                m_mutex.unlock();
                //return false 的地方并不是在 while 循环内部，而是在等待过程中发生了异常情况时才会执行的
                return false;
            }
        }
        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;

        m_mutex.unlock();
        return true;
    }
    //增加的超时处理
    bool pop(T &item, int ms_timeout){
        struct timespec t = {0, 0};
        struct timeval now = {0, 0};
        //获取当前的时间戳，以便计算超时时间
        gettimeofday(&now, NULL);

        m_mutex.lock();
        //如果队列为空，函数会计算出超时时间t，然后调用条件变量的 timewait 函数,等待一段时间，直到超时或者有新元素加入队列
        if (m_size <= 0){
            t.tv_sec = now.tv_sec + ms_timeout / 1000;
            t.tv_nsec = (ms_timeout % 1000) * 1000;
            //如果等待过程中发生了超时或者其他异常，timewait 函数会返回 false，表示等待失败。这时，函数会重新获取互斥锁，并返回 false，表示取出元素失败
            if (!m_cond.timedwait(m_mutex.get(), t)){
                m_mutex.unlock();
                return false;
            }
        }

        if (m_size <= 0){
            m_mutex.unlock();
            return false;
        }

        m_front = (m_front + 1) % m_max_size;
        item = m_array[m_front];
        m_size--;

        m_mutex.unlock();
        return true;
    }   
private:
    locker m_mutex;
    cond m_cond;

    T* m_array;
    int m_size;
    int m_max_size;
    int m_front;
    int m_back;
};

#endif