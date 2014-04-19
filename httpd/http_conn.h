#ifndef HTTPCONNECTION_H
#define HTTPCONNECTION_H

#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <sys/stat.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <errno.h>
#include "locker.h"//互斥量和信号量的简单封装

class http_conn//HTTP连接任务类型，用于线程池工作队列的任务类型T
{
public:
    static const int FILENAME_LEN = 200;//文件名最大长度，文件是HTTP请求的资源页文件
    static const int READ_BUFFER_SIZE = 2048;//读缓冲区，用于读取HTTP请求
    static const int WRITE_BUFFER_SIZE = 1024;//写缓冲区，用于HTTP回答
    enum METHOD { GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT, PATCH };//HTTP请求方法，本程序只定义了GET逻辑
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };//HTTP请求状态：正在解析请求行、正在解析头部、解析中
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };//HTTP请求结果：未完整的请求(客户端仍需要提交请求)、完整的请求、错误请求...只用了前三个
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };//HTTP每行解析状态：改行解析完毕、错误的行、正在解析行

public:
    http_conn(){}
    ~http_conn(){}

public:
    void init( int sockfd, const sockaddr_in& addr );//初始化新的HTTP连接
    void close_conn( bool real_close = true );
    void process();//处理客户请求
    bool read();//读取客户发送来的数据(HTTP请求)
    bool write();//将请求结果返回给客户端

private:
    void init();//重载init初始化连接，用于内部调用
    HTTP_CODE process_read();//解析HTTP请求,内部调用parse_系列函数
    bool process_write( HTTP_CODE ret );//填充HTTP应答，通常是将客户请求的资源页发送给客户，内部调用add_系列函数

    HTTP_CODE parse_request_line( char* text );//解析HTTP请求的请求行
    HTTP_CODE parse_headers( char* text );//解析HTTP头部数据
    HTTP_CODE parse_content( char* text );//获取解析结果
    HTTP_CODE do_request();//处理HTTP连接：内部调用process_read(),process_write()
    char* get_line() { return m_read_buf + m_start_line; }//获取HTTP请求数据中的一行数据
    LINE_STATUS parse_line();//解析行内部调用parse_request_line和parse_headers
    //下面的函数被process_write填充HTTP应答   
    void unmap();//解除内存映射，这里内存映射是指将客户请求的资源页文件映射通过mmap映射到内存
    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();

public:
    static int m_epollfd;//所有socket上的事件都注册到一个epoll事件表中所以用static
    static int m_user_count;//用户数量

private:
    int m_sockfd;//HTTP连接对应的客户在服务端的描述符m_sockfd和地址m_address
    sockaddr_in m_address;

    char m_read_buf[ READ_BUFFER_SIZE ];//读缓冲区，读取HTTP请求
    int m_read_idx;//已读入的客户数据最后一个字节的下一个位置，即未读数据的第一个位置
    int m_checked_idx;//当前已经解析的字节(HTTP请求需要逐个解析)
    int m_start_line;//当前解析行的起始位置
    char m_write_buf[ WRITE_BUFFER_SIZE ];//写缓冲区
    int m_write_idx;//写缓冲区待发送的数据

    CHECK_STATE m_check_state;//HTTP解析的状态：请求行解析、头部解析
    METHOD m_method;//HTTP请求方法，只实现了GET

    char m_real_file[ FILENAME_LEN ];//HTTP请求的资源页对应的文件名称，和服务端的路径拼接就形成了资源页的路径
    char* m_url;//请求的具体资源页名称，如：www.baidu.com/index.html
    char* m_version;//HTTP协议版本号，一般是:HTTP/1.1
    char* m_host;//主机名，客户端要在HTTP请求中的目的主机名
    int m_content_length;//HTTP消息体的长度，简单的GET请求这个为空
    bool m_linger;//HTTP请求是否保持连接

    char* m_file_address;//资源页文件内存映射后的地址
    struct stat m_file_stat;//资源页文件的状态，stat文件结构体
    struct iovec m_iv[2];//调用writev集中写函数需要m_iv_count表示被写内存块的数量，iovec结构体存放了一段内存的起始位置和长度，
    int m_iv_count;//m_iv_count是指iovec结构体数组的长度即多少个内存块
};

#endif