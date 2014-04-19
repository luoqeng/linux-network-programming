编译：gcc http.c -o http -lpthread
运行：./http 5000

#include <stdio.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

//#include "Socks5.h"

#define MAX_USER 10
#define BUFF_SIZE 1024
#define TIME_OUT 6000000
#define MAXLENGTH 1500
#define ERR(p,sock,str) {free(p);close(sock);printf("error: %s",str);return -1;}
pthread_mutex_t mut;
pthread_mutex_t mut;
int counter = 0;
int server_port;
//int Connect_server();


//string replace    :    length_src >= length_dst

void repstr(char *data, char *src, char *dst)
{
    int length_src,length_dst;
    char *tmp, *tmp1;
    length_src = strlen(src);
    length_dst = strlen(dst);
    tmp = strstr(data,src);
    tmp1 = tmp + length_src;
    if(tmp != NULL)
        if(length_src >= length_dst)        //length_src >= length_dst

        {
            memcpy(tmp, dst, length_dst);
            tmp = tmp + length_dst;
            while(*tmp1 != '\0')
                *tmp++ = *tmp1++;
            while(*tmp!='\0')*tmp++='\0';
        }
        
}

int ForwardData( int sock, int real_server_sock )
{
    char recv_buffer[BUFF_SIZE] = { 0 };
    fd_set fd_read;
    struct timeval time_out;
//    time_out.tv_sec = 2;

//    time_out.tv_usec = TIME_OUT;

    int ret = 0,i=0;
    while( 1 )
    {
        time_out.tv_sec = 0;
         time_out.tv_usec = TIME_OUT;
        FD_ZERO( &fd_read );
        FD_SET( sock, &fd_read );
        FD_SET( real_server_sock, &fd_read );
        ret = select( (sock > real_server_sock ? sock : real_server_sock) + 1 ,
        &fd_read, NULL, NULL, &time_out );
        if( -1 == ret )
        {
            perror( "select socket error" );
            break;
        }
        else if( 0 == ret )
        {
    //        perror( "select time out" );

            printf("select time out. i = %d\n",i++);
            continue;
        }
    //printf( "[DEBUG] testing readable!\n" );

        if( FD_ISSET(sock, &fd_read) )
        {
            //printf( "client can read!\n" );

            memset( recv_buffer, 0, BUFF_SIZE );
            ret = recv( sock, recv_buffer, BUFF_SIZE, 0 );
            if( ret > 0 )
            {
/*                int j=0;
                printf( "recv %d bytes from client.\n", ret );
                printf("*****************\n");
                for(j = 0; j < ret;j++)
                printf( "%x ", recv_buffer[j] );
                printf( "****************\n");
*/                printf("client to server ..........thread_id = %u\n",pthread_self());
                ret = send( real_server_sock, recv_buffer, ret, 0 );
                if( ret == -1 )
                {
                    perror( "send data to real server error" );
                    break;
                }
            //printf( "send %d bytes to client!\n", ret );

            }
            else if( ret == 0 )
            {
                //printf( "client close socket.\n" );

                break;
            }
            else
            {
                //perror( "recv from client error" );

                break;
            }
        }
        else if( FD_ISSET(real_server_sock, &fd_read) )
        {
            //printf( "real server can read!\n" );

            memset( recv_buffer, 0, BUFF_SIZE );
            ret = recv( real_server_sock, recv_buffer, BUFF_SIZE, 0 );
            if( ret > 0 )
            {
                //printf( "%s", recv_buffer );

                //printf( "recv %d bytes from real server.\n", ret );

                printf("....................server to client....thread_id = %u\n",pthread_self());
                ret = send( sock, recv_buffer, ret, 0 );
                if( ret == -1 )
                {
                    perror( "send data to client error" );
                    break;
                }
            }
            else if( ret == 0 )
            {
                //printf( "real server close socket.\n" );

                break;
            }
            else
            {
                perror( "recv from real server error" );
                break;
            }
        }
    }
    return 0;
}
unsigned long GetDomainIp(char domainname[250])
{
    struct sockaddr_in sin;
    struct hostent *phost = gethostbyname( domainname );
    printf("domainname is %s\n",domainname);
    if( phost == NULL )
    {
            printf( "gethostbyname error\n");
//            close( sock );

            return 0;
    }
    memcpy( &sin.sin_addr , phost->h_addr_list[0] , phost->h_length );
    return sin.sin_addr.s_addr;
}
//thread for a client connect

int Httpp( void *client_sock )
{
    int sock = *(int *)client_sock,ConnectSock;
    char url[400],temp[400],httpurl[400],portnum[10];
    int datalen,i,port;
    struct sockaddr_in remotesock_addr;
    char *index_start,*index_end,*data;
    bzero(url,400);
    bzero(temp,400);
    bzero(httpurl,400);
    bzero(portnum,10);
    data = (char *)malloc(MAXLENGTH);
    if(data == NULL)
        ERR(NULL,sock,"Http:data malloc fail\n");
    bzero(data,MAXLENGTH);
    datalen = recv(sock,data,MAXLENGTH,0);
    if(datalen<=0)
        ERR(data,sock,"Http:datalen<=0\n");
//    printf("...................recv data.............\n%s\n",data);

//    printf("end data\n");

    index_start = strstr(data,"Host: ");
    index_end = strstr(index_start,"\r\n");
    if(index_start == NULL || index_end == NULL)
        ERR(data,sock,"index_start or index_end is NULL\n");
//    else

//        printf("Host: %s");

//    printf("%p\t%p\t%d",index_start,index_end,(int)(index_end-index_start));

    if((i=(int)(index_end-index_start)) <= 0)
        ERR(data,sock,"index_end-index_start <= 0\n")
    strncpy(url, index_start+6, i-6);
    printf("Host: %s.........success\n",url);
    strcpy(temp,url);
    strcat(temp,":");
    datalen = strlen(temp);
    
    if(strstr(data,"GET") != NULL)
    {
//        printf("Now GET start\n");

        strcpy(httpurl,"http://");
        index_start=strstr(data,temp);
        if(index_start != NULL)
        {
            index_end = strstr(index_start,"/");
            if(index_end == NULL)
                ERR(data,sock,"index_end == NULL");
            if(!sscanf(index_start,"%d",&port))
                ERR(data,sock,"sscanf port fail\n");
            sprintf(portnum,"%s",port);
            strcat(httpurl,temp);
            strcat(httpurl,portnum);
            strcat(httpurl,"/");
        }
        else
        {
            port=80;
            strcat(httpurl,url);
            strcat(httpurl,"/");;
        }
        printf("GET...httpurl:%s\n",httpurl);
        repstr(data,httpurl,"/");
//        printf("..GET..\n%s\n",data);

//        printf("..GET..\n");

        repstr(data,"HTTP/1.0","HTTP/1.1");
    //    printf("...end GET ...\n%s\n",data);

        //字符串替换

    }
    else if(strstr(data,"POST") != NULL)
    {
        strcpy(httpurl,"http://");
        index_start=strstr(data,temp);
        if(index_start != NULL)
        {
            index_end = strstr(index_start,"/");
            if(index_end == NULL)
                ERR(data,sock,"index_end == NULL");
            if(!sscanf(index_start,"%d",&port))
                ERR(data,sock,"sscanf port fail\n");
            sprintf(portnum,"%s",port);
            strcat(httpurl,temp);
            strcat(httpurl,portnum);
            strcat(httpurl,"/");
        }
        else
        {
            port=80;
            strcat(httpurl,url);
            strcat(httpurl,"/");;
        }
        printf("POST...httpurl:%s\n",httpurl);
        repstr(data,httpurl,"/");
    }
    else if(strstr(data,"CONNECT") != NULL)
    {
        index_start=strstr(data,temp);
        if(index_start != NULL)
        {
            index_end = strstr(index_start," ");
            if(index_end == NULL)
                ERR(data,sock,"index_end == NULL");
            if(!sscanf(index_start,"%d",&port))
                ERR(data,sock,"sscanf port fail\n");
            printf("CONNECT...port:%d\n",port);
        }
        else
            ERR(data,sock,"CONNECT:index_start == NULL\n");
    }
    else
        ERR(data,sock,"Not GET POST CONNECT\n");
    
    remotesock_addr.sin_family =AF_INET;
    remotesock_addr.sin_port = htons(port);
    remotesock_addr.sin_addr.s_addr = GetDomainIp(url);//GetDomainIp()

    if(remotesock_addr.sin_addr.s_addr == 0)
        ERR(data,sock,"GetDomainIp() failed\n");
//    else

        printf("GetDomainIp() success\n");
    ConnectSock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
//    printf("now connect..................\n");        

    if(connect(ConnectSock,(struct sockaddr *)&remotesock_addr,sizeof(remotesock_addr)))
        ERR(data,sock,"connect fail\n\n");
//    else

        printf("connect server success\n");
//    bzero(data,MAXLENGTH);

    datalen = strlen(data);
    if(strstr(data,"CONNECT") == NULL)
    {
        printf(".........data send to server........\t");
        if(send(ConnectSock,data,datalen,0)==strlen(data))
            printf("success\n");
        else
            printf("fail\n");
    }
    else
    {
        bzero(data,MAXLENGTH);
        strcpy(data,"HTTP/1.0 200 Connection established\r\nProxy-agent: CHTTPPROXY V1.0 powered by shadow\r\n\r\n");
        datalen=strlen(data);
        printf("...data to client........\n");
        send(sock,data,datalen,0);
    }
    printf("sock=%d\tConnectSock=%d\n",sock,ConnectSock);
    ForwardData( sock, ConnectSock );
    close( sock );
    close( ConnectSock );
    pthread_mutex_lock(&mut);
    counter--;
    printf("a connect end ........id = %u..................... pthread number = %d\n",pthread_self(),counter);
    pthread_mutex_unlock(&mut);
    return 0;
}
int main( int argc, char *argv[] )
{
    if( argc != 2 )
    {
        printf( "Http proxy for test,code by LanKong\n" );
        printf( "Usage: %s <proxy_port>\n", argv[0] );
        printf( "Options:\n" );
        printf( " <proxy_port> ---which port of this proxy server will listen.\n" );
        return 1;
    }
    pthread_mutex_init(&mut,NULL);
    struct sockaddr_in sin;
    memset( (void *)&sin, 0, sizeof( struct sockaddr_in) );
    sin.sin_family = AF_INET;
    sin.sin_port = htons( atoi(argv[1]) );
    server_port = sin.sin_port;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    int listen_sock = socket( AF_INET, SOCK_STREAM, 0 );
    if( listen_sock < 0 )
    {
        perror( "Socket creation failed\n");
        return -1;
    }
    int opt = SO_REUSEADDR;
    setsockopt( listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if( bind( listen_sock, (struct sockaddr*)&sin, sizeof(struct sockaddr_in) ) < 0 )
    {
        perror( "Bind error" );
        return -1;
    }
    if( listen( listen_sock, MAX_USER ) < 0 )
    {
        perror( "Listen error" );
        return -1;
    }
    struct sockaddr_in cin;
    int client_sock;
    int client_len = sizeof( struct sockaddr_in );
    while( client_sock = accept( listen_sock, (struct sockaddr *)&cin,(socklen_t *)&client_len ) )
    {
    //    printf( "Connected from %s, processing......",inet_ntoa( cin.sin_addr ) );

        pthread_t work_thread;
        if( pthread_create( &work_thread, NULL, (void *)Httpp, (void*)&client_sock ) )
        {
            perror( "Create thread error..." );
            close( client_sock );
        }
        else
        {
            pthread_mutex_lock(&mut);
            counter++;
            printf("Connect from %s,processing...id = %u.... pthread number = %d\n",inet_ntoa( cin.sin_addr ),work_thread,counter);
            pthread_mutex_unlock(&mut);
            pthread_detach( work_thread );
        }
    }
    pthread_mutex_destroy(&mut);
}