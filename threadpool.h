#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include<cstdio>
#include <list>
//#include <semaphore.h>
#include<exception>
#include "locker.h"

// 线程池类，将它定义为模板类是为了代码复用，模板参数T是任务类
template <typename T>
class threadpool
{
private:
    int m_thread_number;        // 线程的数量
    pthread_t *m_threads;        // 描述线程池的数组，大小为m_thread_number    
    int m_max_requests;         // 请求队列中最多允许的、等待处理的请求的数量
    std::list<T *> m_workqueue; // 请求队列
    locker m_queuelocker;       // 保护请求队列的互斥锁
    sem m_queuestat;            // 是否有任务需要处理
    bool m_stop;                // 是否结束线程

private:
    static void* worker(void* arg);
    void run();
public:
    threadpool(int thread_number = 8, int max_request = 10000);
    ~threadpool();
    bool append(T *request); // 添加任务
};

template <typename T>
threadpool<T>::threadpool(int thread_number, int max_request):
    m_thread_number(thread_number), m_max_requests(max_request),
    m_stop(false), m_threads(NULL)
    {
        if((thread_number <= 0) || (max_request <= 0))
        {
            throw std::exception();
        }

        m_threads = new pthread_t[m_thread_number];
        if(!m_threads)
        {
            throw std::exception();
        }
        // 创建thread_number 个线程，并将他们设置为脱离线程
        for(int i = 0; i < thread_number; ++i)
        {
            printf("Create the %dth thread.\n",i);

            if(pthread_create(m_threads + i, NULL, worker, this) != 0)
            {
                delete [] m_threads;
                throw std::exception();
            }

            if(pthread_detach(m_threads[i]))
            {
                delete [] m_threads;
                throw std::exception();
            }
        }
    }

template <typename T>
threadpool<T>::~threadpool()
{
    delete[] m_threads;
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T* request)
{
    m_queuelocker.lock();
    if(m_workqueue.size() > m_max_requests)
    {
        m_queuelocker.unlock();
        return false;
    }

    m_workqueue.push_back(request);
    m_queuelocker.unlock();
    m_queuestat.post();
    return true;
}

template <typename T>
void* threadpool<T>::worker(void* arg)
{
    threadpool* pool = (threadpool*) arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run()
{
    while(!m_stop)
    {
        m_queuestat.wait();
        m_queuelocker.lock();
        if(m_workqueue.empty())
        {
            m_queuelocker.unlock();
            continue;
        }

        T* request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queuelocker.unlock();

        if(!request)
        {
            continue;
        }

        request->process();
    }
}

#endif