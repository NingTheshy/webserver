#ifndef LOCKER_H
#define LOCKER_H

//线程同步机制

#include<exception>
#include<pthread.h>
#include<semaphore.h>

//互斥锁
class locker{
public:
    locker(){
        if(pthread_mutex_init(&m_mutex,NULL) != 0){
            throw std::exception();
        }
    }
    ~locker(){
        pthread_mutex_destroy(&m_mutex);
    }

    //上锁
    bool lock(){
        return pthread_mutex_lock(&m_mutex) == 0;
    }
    //解锁
    bool unlock(){
        return pthread_mutex_unlock(&m_mutex) == 0;
    }
    //获取成员的互斥量
    pthread_mutex_t* get(){
        return &m_mutex;
    }
    
private:
    pthread_mutex_t m_mutex;
};

//条件变量
class cond{
public:
    cond(){
        if(pthread_cond_init(&m_cond, NULL) != 0){
            throw std::exception();
        }
    }
    ~cond(){
        pthread_cond_destroy(&m_cond);
    }
    
    bool wait(pthread_mutex_t* m_mutex){
        int ret = 0;
        ret = pthread_cond_wait(&m_cond, m_mutex);
        return ret == 0;
    }
    bool timedwait(pthread_mutex_t* m_mutex, struct timespec time){
        int ret = 0;
        ret = pthread_cond_timedwait(&m_cond, m_mutex, &time);
        return ret == 0;
    }
    //增加条件变量
    bool signal(){
        return pthread_cond_signal(&m_cond) == 0;
    }
    //将所有线程都唤醒
    bool broadcast(){
        return pthread_cond_broadcast(&m_cond) == 0;
    }
private:
    pthread_cond_t m_cond;
};

//信号量
class sem{
public:
    sem(){
        if(sem_init(&m_sem,0,0) != 0){
            throw std::exception();
        }
    }
    sem(int num){
        if(sem_init(&m_sem,0,num) != 0){
            throw std::exception();
        }
    }
    ~sem(){
        sem_destroy(&m_sem);
    }

    //等待信号量
    bool wait(){
        return sem_wait(&m_sem) == 0;
    }
    //增加信号量
    bool post(){
        return sem_post(&m_sem) == 0;
    }
private:
    sem_t m_sem;
};

#endif