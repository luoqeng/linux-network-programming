#include "http_conn.h"
//定义了HTTP请求的返回状态信息，类似大家都熟悉的404
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";
const char* doc_root = "/var/www/html";//服务端资源页的路径，将其和HTTP请求中解析的m_url拼接形成资源页的位置

int setnonblocking( int fd )//将fd设置为非阻塞
{
    int old_option = fcntl( fd, F_GETFL );
    int new_option = old_option | O_NONBLOCK;
    fcntl( fd, F_SETFL, new_option );
    return old_option;
}

void addfd( int epollfd, int fd, bool one_shot )//将fd添加到事件表epollfd
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if( one_shot )//采用EPOLLONESHOT事件避免了同一事件被多次触发，因为一个事件只被触发一次且需要重置事件才能侦听下次是否发生
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl( epollfd, EPOLL_CTL_ADD, fd, &event );
    setnonblocking( fd );
}

void removefd( int epollfd, int fd )//将fd从事件表epollfd中移除
{
    epoll_ctl( epollfd, EPOLL_CTL_DEL, fd, 0 );
    close( fd );
}

void modfd( int epollfd, int fd, int ev )//EPOLLONESHOT需要重置事件后事件才能进行下次侦听
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl( epollfd, EPOLL_CTL_MOD, fd, &event );//注意是EPOLL_CTL_MOD修改
}

int http_conn::m_user_count = 0;//连接数
int http_conn::m_epollfd = -1;//事件表，注意是static故所有http_con类对象共享一个事件表

void http_conn::close_conn( bool real_close )//关闭连接，从事件表中移除描述符
{
    if( real_close && ( m_sockfd != -1 ) )//m_sockfd是该HTTP连接对应的描述符
    {
        //modfd( m_epollfd, m_sockfd, EPOLLIN );
        removefd( m_epollfd, m_sockfd );
        m_sockfd = -1;
        m_user_count--;
    }
}

void http_conn::init( int sockfd, const sockaddr_in& addr )//初始化连接
{
    m_sockfd = sockfd;//sockfd是http连接对应的描述符用于接收http请求和http回答
    m_address = addr;//客户端地址
    int error = 0;
    socklen_t len = sizeof( error );
    getsockopt( m_sockfd, SOL_SOCKET, SO_ERROR, &error, &len );
    int reuse = 1;
    setsockopt( m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof( reuse ) );//获取描述符状态，可以在调试时用
    addfd( m_epollfd, sockfd, true );
    m_user_count++;//多了一个http用户

    init();//调用重载函数
}

void http_conn::init()//重载init函数进行些连接前的初始化操作
{
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset( m_read_buf, '\0', READ_BUFFER_SIZE );
    memset( m_write_buf, '\0', WRITE_BUFFER_SIZE );
    memset( m_real_file, '\0', FILENAME_LEN );
}

http_conn::LINE_STATUS http_conn::parse_line()//解析HTTP数据：将HTTP数据的每行数据提取出来，每行以回车\r和换行符\n结束
{
    char temp;
    for ( ; m_checked_idx < m_read_idx; ++m_checked_idx )//m_checked_idx是当前正在解析的字节，m_read_idx是读缓冲区中已有的数据(客户端发送了多少HTTP请求数据到来),解析到m_read_idx号字节
    {
        temp = m_read_buf[ m_checked_idx ];//当前解析字节
        if ( temp == '\r' )//若为回车符：
        {
            if ( ( m_checked_idx + 1 ) == m_read_idx )//若为回车符：若此回车符是已读取数据的最后一个则仍需要解析改行(即该行数据还没有接收完整)
            {
                return LINE_OPEN;
            }
            else if ( m_read_buf[ m_checked_idx + 1 ] == '\n' )//若回车符的下一个是换行符\r则表明该行解析完毕(回车+换行是HTTP请求每行固定结束规则)
            {
                m_read_buf[ m_checked_idx++ ] = '\0';//将该行数据送给缓冲区
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;//返回状态：该行解析成功
            }

            return LINE_BAD;//否则解析失败
        }
        else if( temp == '\n' )//解析的字符是换行符则前一个必须是回车才解析成功
        {
            if( ( m_checked_idx > 1 ) && ( m_read_buf[ m_checked_idx - 1 ] == '\r' ) )
            {
                m_read_buf[ m_checked_idx-1 ] = '\0';
                m_read_buf[ m_checked_idx++ ] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    return LINE_OPEN;//正在解析，还有HTTP请求数据没有接收到....
}

bool http_conn::read()//读取HTTP请求数据
{
    if( m_read_idx >= READ_BUFFER_SIZE )//读缓冲区已满
    {
        return false;
    }

    int bytes_read = 0;//记录接收的字节数
    while( true )//循环读取的原因是EPOLLONESHOT一个事件只触发一次所以需要一次性读取完全否则数据丢失
    {
        bytes_read = recv( m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0 );//接收客户端的HTTP请求
        if ( bytes_read == -1 )
        {
            if( errno == EAGAIN || errno == EWOULDBLOCK )//非阻塞描述符这两个errno不是网络出错而是设备当前不可得，在这里就是一次事件的数据读取完毕
            {
                break;
            }
            return false;//否则recv出错
        }
        else if ( bytes_read == 0 )//客户端关闭了连接
        {
            return false;
        }

        m_read_idx += bytes_read;//更新读缓冲区的已读大小(用于解析行函数)
    }
    return true;
}

http_conn::HTTP_CODE http_conn::parse_request_line( char* text )//解析HTTP的请求行部分
{
    m_url = strpbrk( text, " \t" );//在text搜索\t的位置
    if ( ! m_url )
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0';

    char* method = text;
    if ( strcasecmp( method, "GET" ) == 0 )//忽略大小写比较mehtod和GET的大小返回值和strcmp定义相同
    {
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST;
    }

    m_url += strspn( m_url, " \t" );//strspn函数是在m_url找到第一个\t位置，拼接资源页文件路径
    m_version = strpbrk( m_url, " \t" );
    if ( ! m_version )
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn( m_version, " \t" );
    if ( strcasecmp( m_version, "HTTP/1.1" ) != 0 )//HTTP版本
    {
        return BAD_REQUEST;
    }

    if ( strncasecmp( m_url, "http://", 7 ) == 0 )
    {
        m_url += 7;
        m_url = strchr( m_url, '/' );
    }

    if ( ! m_url || m_url[ 0 ] != '/' )
    {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;//将HTTP解析状态更新为解析头部，那么HTTP解析进入解析HTTP头部。这是有限状态机
    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_headers( char* text )//解析HTTP头部
{
    if( text[ 0 ] == '\0' )
    {
        if ( m_method == HEAD )
        {
            return GET_REQUEST;//已经获取了一个完整的HTTP请求
        }

        if ( m_content_length != 0 )//若HTTP请求消息长度不为空
        {
            m_check_state = CHECK_STATE_CONTENT;//则解析头部后还要解析消息体，所以HTTP解析状态仍为正在解析中...GET请求不会出现这个...
            return NO_REQUEST;
        }

        return GET_REQUEST;
    }
    else if ( strncasecmp( text, "Connection:", 11 ) == 0 )
    {
        text += 11;
        text += strspn( text, " \t" );
        if ( strcasecmp( text, "keep-alive" ) == 0 )
        {
            m_linger = true;
        }
    }
    else if ( strncasecmp( text, "Content-Length:", 15 ) == 0 )
    {
        text += 15;
        text += strspn( text, " \t" );
        m_content_length = atol( text );
    }
    else if ( strncasecmp( text, "Host:", 5 ) == 0 )
    {
        text += 5;
        text += strspn( text, " \t" );
        m_host = text;
    }
    else
    {
        printf( "oop! unknow header %s\n", text );
    }

    return NO_REQUEST;

}

http_conn::HTTP_CODE http_conn::parse_content( char* text )//解析结果
{
    if ( m_read_idx >= ( m_content_length + m_checked_idx ) )//若解析到缓冲区的最后位置则获得一个一个完整的连接请求
    {
        text[ m_content_length ] = '\0';
        return GET_REQUEST;
    }

    return NO_REQUEST;//请求不完整
}

http_conn::HTTP_CODE http_conn::process_read()//完整的HTTP解析
{
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char* text = 0;

    while ( ( ( m_check_state == CHECK_STATE_CONTENT ) && ( line_status == LINE_OK  ) )
                || ( ( line_status = parse_line() ) == LINE_OK ) ){//满足条件：正在进行HTTP解析、读取一个完整行
        text = get_line();//从读缓冲区(HTTP请求数据)获取一行数据
        m_start_line = m_checked_idx;//行的起始位置等于正在每行解析的第一个字节
        printf( "got 1 http line: %s\n", text );

        switch ( m_check_state )//HTTP解析状态跳转
        {
            case CHECK_STATE_REQUESTLINE://正在分析请求行
            {
                ret = parse_request_line( text );//分析请求行
                if ( ret == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADER://正在分析请求头部
            {
                ret = parse_headers( text );//分析头部
                if ( ret == BAD_REQUEST )
                {
                    return BAD_REQUEST;
                }
                else if ( ret == GET_REQUEST )
                {
                    return do_request();//当获得一个完整的连接请求则调用do_request分析处理资源页文件
                }
                break;
            }
            case CHECK_STATE_CONTENT://HTTP解析状态仍为正在解析...没有办法只好继续解析呗....解析消息体
            {
                ret = parse_content( text );
                if ( ret == GET_REQUEST )
                {
                    return do_request();
                }
                line_status = LINE_OPEN;
                break;
            }
            default:
            {
                return INTERNAL_ERROR;//内部错误
            }
        }
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()//用于获取资源页文件的状态
{
    strcpy( m_real_file, doc_root );
    int len = strlen( doc_root );
    strncpy( m_real_file + len, m_url, FILENAME_LEN - len - 1 );
    if ( stat( m_real_file, &m_file_stat ) < 0 )
    {
        return NO_RESOURCE;//若资源页不存在则HTTP解析结果为：没有资源...万恶的404
    }

    if ( ! ( m_file_stat.st_mode & S_IROTH ) )
    {
        return FORBIDDEN_REQUEST;//资源没有权限获取
    }

    if ( S_ISDIR( m_file_stat.st_mode ) )
    {
        return BAD_REQUEST;//请求有错
    }

    int fd = open( m_real_file, O_RDONLY );
    m_file_address = ( char* )mmap( 0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0 );//将资源页文件映射到内存
    close( fd );
    return FILE_REQUEST;//资源页请求成功
}

void http_conn::unmap()//解除资源页文件映射的内存
{
    if( m_file_address )
    {
        munmap( m_file_address, m_file_stat.st_size );//解除映射
        m_file_address = 0;
    }
}

bool http_conn::write()//将资源页文件发送给客户端
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if ( bytes_to_send == 0 )
    {
        modfd( m_epollfd, m_sockfd, EPOLLIN );//EPOLLONESHOT事件每次需要重置事件
        init();
        return true;
    }

    while( 1 )//
    {
        temp = writev( m_sockfd, m_iv, m_iv_count );//集中写，m_sockfd是http连接对应的描述符，m_iv是iovec结构体数组表示内存块地址，m_iv_count是数组的长度即多少个内存块将一次集中写到m_sockfd
        if ( temp <= -1 )//集中写失败
        {
            if( errno == EAGAIN )
            {
                modfd( m_epollfd, m_sockfd, EPOLLOUT );//重置EPOLLONESHOT事件,注册可写事件表示若m_sockfd没有写失败则关闭连接
                return true;
            }
            unmap();//解除内存映射
            return false;
        }

        bytes_to_send -= temp;//待发送数据
        bytes_have_send += temp;//已发送数据
        if ( bytes_to_send <= bytes_have_send )
        {
            unmap();//该资源页已经发送完毕该解除映射
            if( m_linger )//若要保持该http连接
            {
                init();//初始化http连接
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return true;
            }
            else
            {
                modfd( m_epollfd, m_sockfd, EPOLLIN );
                return false;
            } 
        }
    }
}

bool http_conn::add_response( const char* format, ... )//HTTP应答主要是将应答数据添加到写缓冲区m_write_buf
{
    if( m_write_idx >= WRITE_BUFFER_SIZE )
    {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );//将fromat内容输出到m_write_buf
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) )
    {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_conn::add_status_line( int status, const char* title )
{
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );//
}

bool http_conn::add_headers( int content_len )
{
    add_content_length( content_len );
    add_linger();
    add_blank_line();//加空行：回车+换行
}

bool http_conn::add_content_length( int content_len )
{
    return add_response( "Content-Length: %d\r\n", content_len );//
}

bool http_conn::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );//
}

bool http_conn::add_blank_line()
{
    return add_response( "%s", "\r\n" );//
}

bool http_conn::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_conn::process_write( HTTP_CODE ret )//填充HTTP应答
{
    switch ( ret )
    {
        case INTERNAL_ERROR:
        {
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) )
            {
                return false;
            }
            break;
        }
        case BAD_REQUEST:
        {
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) )
            {
                return false;
            }
            break;
        }
        case NO_RESOURCE:
        {
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) )
            {
                return false;
            }
            break;
        }
        case FORBIDDEN_REQUEST:
        {
            add_status_line( 403, error_403_title );
            add_headers( strlen( error_403_form ) );
            if ( ! add_content( error_403_form ) )
            {
                return false;
            }
            break;
        }
        case FILE_REQUEST://资源页文件可用
        {
            add_status_line( 200, ok_200_title );
            if ( m_file_stat.st_size != 0 )
            {
                add_headers( m_file_stat.st_size );//m_file_stat资源页文件状态
                m_iv[ 0 ].iov_base = m_write_buf;//写缓冲区
                m_iv[ 0 ].iov_len = m_write_idx;//长度
                m_iv[ 1 ].iov_base = m_file_address;//资源页数据内存映射后在m_file_address地址
                m_iv[ 1 ].iov_len = m_file_stat.st_size;//文件长度就是该块内存长度
                m_iv_count = 2;
                return true;
            }
            else
            {
                const char* ok_string = "<html><body></body></html>";//请求页位空白
                add_headers( strlen( ok_string ) );
                if ( ! add_content( ok_string ) )
                {
                    return false;
                }
            }
        }
        default:
        {
            return false;
        }
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

void http_conn::process()//处理HTTP请求
{
    HTTP_CODE read_ret = process_read();//读取HTTP请求数据
    if ( read_ret == NO_REQUEST )
    {
        modfd( m_epollfd, m_sockfd, EPOLLIN );
        return;
    }

    bool write_ret = process_write( read_ret );//发送资源页给客户端
    if ( ! write_ret )
    {
        close_conn();
    }

    modfd( m_epollfd, m_sockfd, EPOLLOUT );
}
