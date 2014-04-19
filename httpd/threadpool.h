#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "locker.h"//简单封装了互斥量和信号量的接口

template< typename T >
class threadpool//线程池类模板参数T是任务类型，T中必须有接口process
{
public:
    threadpool( int thread_number = 8, int max_requests = 10000 );//线程数目和最大连接处理数
    ~threadpool();
    bool append( T* request );

private:
    static void* worker( void* arg );//线程工作函数
    void run();//启动线程池

private:
    int m_thread_number;//线程数量
    int m_max_requests;//最大连接数目
    pthread_t* m_threads;//线程id
    std::list< T* > m_workqueue;//工作队列:各线程竞争该队列并处理相应的任务逻辑T
    locker m_queuelocker;//工作队列互斥量
    sem m_queuestat;//信号量：用于工作队列
    bool m_stop;//终止标志
};

template< typename T >
threadpool< T >::threadpool( int thread_number, int max_requests ) : 
        m_thread_number( thread_number ), m_max_requests( max_requests ), m_stop( false ), m_threads( NULL )
{
    if( ( thread_number <= 0 ) || ( max_requests <= 0 ) )
    {
        throw std::exception();
    }

    m_threads = new pthread_t[ m_thread_number ];//工作线程数组
    if( ! m_threads )
    {
        throw std::exception();
    }

    for ( int i = 0; i < thread_number; ++i )//创建工作线程
    {
        printf( "create the %dth thread\n", i );
        if( pthread_create( m_threads + i, NULL, worker, this ) != 0 )//注意C++调用pthread_create函数的第三个参数必须是一个静态函数，一个静态成员使用动态成员的方式：通过类静态对象、将类对象作为参数传给静态函数。这里使用了后者所以有this
        {
            delete [] m_threads;
            throw std::exception();
        }
        if( pthread_detach( m_threads[i] ) )//线程分离后其它线程无法pthread_join等待
        {
            delete [] m_threads;
            throw std::exception();
        }
    }
}

template< typename T >
threadpool< T >::~threadpool()
{
    delete [] m_threads;
    m_stop = true;
}

template< typename T >
bool threadpool< T >::append( T* request )//向工作队列添加任务T
{
    m_queuelocker.lock();//非原子操作需要互斥量保护
    if ( m_workqueue.size() > m_max_requests )//任务队列满了
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back( request );
    m_queuelocker.unlock();
    m_queuestat.post();//信号量的V操作，即信号量+1多了个工作任务T
    return true;
}

template< typename T >
void* threadpool< T >::worker( void* arg )//工作线程函数
{
    threadpool* pool = ( threadpool* )arg;//获取进程池对象
    pool->run();//调用线程池run函数
    return pool;
}

template< typename T >
void threadpool< T >::run()//工作线程真正工作逻辑：从任务队列领取任务T并执行任务T
{
    while ( ! m_stop )
    {
        m_queuestat.wait();//信号量P操作，申请信号量获取任务T
        m_queuelocker.lock();//互斥量保护任务队列，和前面的信号量顺序不能呼唤。。。你懂的
        if ( m_workqueue.empty() )
        {
            m_queuelocker.unlock();//任务队列空那就没任务呗
            continue;
        }
        T* request = m_workqueue.front();//获取任务T
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if ( ! request )
        {
            continue;
        }
        request->process();//执行任务T的相应逻辑，任务T中必须有process接口
    }
}

#endif