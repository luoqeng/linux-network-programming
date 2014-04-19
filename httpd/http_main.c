#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <cassert>
#include <sys/epoll.h>

#include "locker.h"//该头文件封装了信号量和互斥量
#include "threadpool.h"//半同步/半反应堆线程池
#include "http_conn.h"//HTTP连接任务类T

#define MAX_FD 65536//最大文件数目
#define MAX_EVENT_NUMBER 10000//最大事件数目

extern int addfd( int epollfd, int fd, bool one_shot );//采用http_conn.h的addfd函数
extern int removefd( int epollfd, int fd );//这也是http_conn.h中的函数

void addsig( int sig, void( handler )(int), bool restart = true )//安装信号，用于统一事件源(将信号和IO事件统一监听)
{
    struct sigaction sa;
    memset( &sa, '\0', sizeof( sa ) );
    sa.sa_handler = handler;
    if( restart )
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset( &sa.sa_mask );
    assert( sigaction( sig, &sa, NULL ) != -1 );
}

void show_error( int connfd, const char* info )
{
    printf( "%s", info );
    send( connfd, info, strlen( info ), 0 );
    close( connfd );
}


int main( int argc, char* argv[] )
{
    if( argc <= 2 )
    {
        printf( "usage: %s ip_address port_number\n", basename( argv[0] ) );
        return 1;
    }
    const char* ip = argv[1];
    int port = atoi( argv[2] );

    addsig( SIGPIPE, SIG_IGN );

    threadpool< http_conn >* pool = NULL;
    try
    {
        pool = new threadpool< http_conn >;//创建线程池
    }
    catch( ... )
    {
        return 1;
    }

    http_conn* users = new http_conn[ MAX_FD ];//创建超大的用户HTTP连接任务数组，给定一个http连接的描述符作为下标即可索引到这个任务，空间换时间
    assert( users );
    int user_count = 0;

    int listenfd = socket( PF_INET, SOCK_STREAM, 0 );
    assert( listenfd >= 0 );
    struct linger tmp = { 1, 0 };
    setsockopt( listenfd, SOL_SOCKET, SO_LINGER, &tmp, sizeof( tmp ) );

    int ret = 0;
    struct sockaddr_in address;
    bzero( &address, sizeof( address ) );
    address.sin_family = AF_INET;
    inet_pton( AF_INET, ip, &address.sin_addr );
    address.sin_port = htons( port );

    ret = bind( listenfd, ( struct sockaddr* )&address, sizeof( address ) );
    assert( ret >= 0 );

    ret = listen( listenfd, 5 );
    assert( ret >= 0 );

    epoll_event events[ MAX_EVENT_NUMBER ];
    int epollfd = epoll_create( 5 );//创建事件表
    assert( epollfd != -1 );
    addfd( epollfd, listenfd, false );//将监听端口添加到事件表，false表示不注册EPOLLONESHOT事件，注意不能将监听端口注册为EPOLLONESHOT事件因为该事件每次发生只触发一次，而accept每次只能连接一个客户，那么多个客户连接请求到来，则必然丢失客户连接请求
    http_conn::m_epollfd = epollfd;

    while( true )
    {
        int number = epoll_wait( epollfd, events, MAX_EVENT_NUMBER, -1 );//无限期等待sockfd上的注册事件
        if ( ( number < 0 ) && ( errno != EINTR ) )//若epoll_wait不是因中断EINTR是出错
        {
            printf( "epoll failure\n" );
            break;
        }

        for ( int i = 0; i < number; i++ )
        {
            int sockfd = events[i].data.fd;//获取就绪事件描述符
            if( sockfd == listenfd )//监听端口有可读事件则表明有HTTP请求
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof( client_address );
                int connfd = accept( listenfd, ( struct sockaddr* )&client_address, &client_addrlength );//建立客户连接
                if ( connfd < 0 )
                {
                    printf( "errno is: %d\n", errno );
                    continue;
                }
                if( http_conn::m_user_count >= MAX_FD )//HTTP客户数超过MAX_FD
                {
                    show_error( connfd, "Internal server busy" );
                    continue;
                }
                
                users[connfd].init( connfd, client_address );//利用connfd快速索引到http_conn任务类
            }
            else if( events[i].events & ( EPOLLRDHUP | EPOLLHUP | EPOLLERR ) )
            {
                users[sockfd].close_conn();
            }
            else if( events[i].events & EPOLLIN )//数据可读：
            {
                if( users[sockfd].read() )
                {
                    pool->append( users + sockfd );
                }
                else
                {
                    users[sockfd].close_conn();
                }
            }
            else if( events[i].events & EPOLLOUT )//数据可写，哪里注册了可写EPOLLOUT事件？http_conn工作任务类中write函数将那个http连接的描述符m_sockfd注册了可写事件
            {
                if( !users[sockfd].write() )//若该http_conn任务对应的http连接写失败了则关闭该http连接
                {
                    users[sockfd].close_conn();
                }
            }
            else
            {}
        }
    }

    close( epollfd );
    close( listenfd );//这里要提醒的是listenfd由创建它的函数关闭，谁污染谁治理的原则
    delete [] users;
    delete pool;
    return 0;
}