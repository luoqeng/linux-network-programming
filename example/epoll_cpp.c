#include<iostream>
#include<stdlib.h>
#include<string.h>
#include<sys/types.h>
#include<arpa/inet.h>
#include<sys/epoll.h>
#include <unistd.h>
#include<sys/wait.h>
#include <fcntl.h>
#include<stdio.h>
#include<map>
#include<fstream>
#include<time.h>
#include <unistd.h>
#include <fcntl.h>
#include<queue>
#include <sys/types.h>
#include <dirent.h>
#include<errno.h>
#include<assert.h>
#include<pthread.h>
using namespace std;

#define MAX_CON_CNT 1000
#define MAX_EV_CNT  20

static int SER_PORT;
std::map<int, pid_t> ve;

struct flock* file_lock_fun(short type, short whence)
{
    static struct flock ret;
    ret.l_type = type;
    ret.l_start = 0x00;
    ret.l_whence = whence;
    ret.l_len = 0x05;
    ret.l_pid = getpid();
    return & ret;
}

struct connection
{
    int fd;
    enum {BUF_SIZE = 4096};
    char buf[BUF_SIZE];
    int pos;
    int left;
};

void set_noblock(int  fd)
{
    int opts = fcntl(fd,F_GETFL);
    if(opts < 0x00)
    {
        std::cout<<"set no block failed ....."<<std::endl;
    }
    else
    {
        opts |= O_NONBLOCK;
        fcntl(fd,F_SETFL,opts);
    }
}

int pre_work()
{
    int ser_fd = socket(AF_INET,SOCK_STREAM,0x00);
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(SER_PORT);
    
    int flag = 0x01;
    if(setsockopt(ser_fd,SOL_SOCKET,SO_REUSEADDR, &flag, sizeof(flag)) == -1)
    {
        std::cout<<"reuse failed ..."<<std::endl;
        return -1;
    }

    if( -1 == bind(ser_fd, (struct sockaddr*)&addr, sizeof(addr)))
    {
        std::cout<<"bind failed ....."<<getpid()<<"  "<<SER_PORT<<std::endl;
        exit(0x01);
    }
    
    listen(ser_fd, 0x05);
    set_noblock(ser_fd);
    return ser_fd;
}

int lock(std::string fname)
{
    int m_fd = open(fname.c_str(),O_WRONLY);
    if(m_fd > 0x00 && -1 != fcntl(m_fd,F_SETLK,file_lock_fun(F_WRLCK, SEEK_SET)))
    {
        return m_fd;
    }
    else
    {
        if(m_fd > 0x00)
        {
            close(m_fd);
        }
    }
    
    return -1;
}

void unlock(int m_fd)
{
    if(m_fd > 0x00)
    {
        fcntl(m_fd,F_SETLKW,file_lock_fun(F_UNLCK, SEEK_SET));  
        close(m_fd);
    }
}

std::string get_fname()
{
    std::string filename = "";
    char pbuf[256] = {0x00};
    getcwd(pbuf,sizeof(pbuf));
    strcat(pbuf,"/lk");
    filename = std::string(pbuf);
    return filename;
}

void create_lkfile()
{
    std::string fname = get_fname();
    ofstream ofile(fname.c_str(), ios::trunc);
    if(ofile.is_open())
    {
        ofile<<"filelock"<<std::endl;
        ofile.close();
    }
    else
        std::cout<<"create lkfile failed ......"<<std::endl;
}


void modify_con(connection*& obj, int type, int& ep_fd)
{
    struct epoll_event ev;
    ev.data.ptr = obj;
    ev.events = type | EPOLLET ;
    epoll_ctl(ep_fd,EPOLL_CTL_MOD,obj->fd,&ev);
}

void reset_con(connection*& obj, int& ep_fd, struct epoll_event* ev_arr, int index)
{
    time_t ts = time(NULL);
    std::cout<<"in ----------------- close con......."<<obj->fd<<"  "<<ep_fd<<"     "<<ctime(&ts);      
    if(epoll_ctl(ep_fd, EPOLL_CTL_DEL, obj->fd ,ev_arr + index) != 0x00)
        std::cout<<"EPOLL_CTL_DEL mod failed ..."<<errno<<std::endl;
    close(obj->fd);
    ev_arr[index].data.ptr = NULL;
    delete obj;
    obj = NULL;             
}

void work(int ser_fd)
{
    std::string filename = get_fname();
    
    struct epoll_event ev;
    ev.data.fd = ser_fd;
    ev.events = EPOLLIN | EPOLLET;
    int ep_fd = epoll_create(MAX_CON_CNT);
    epoll_ctl(ep_fd, EPOLL_CTL_ADD, ser_fd, &ev);
    
    std::cout<<"create ------------"<<ep_fd<<"    "<<getpid()<<std::endl;
    struct epoll_event ev_arr[MAX_EV_CNT];
    while(1)
    {
        int cnt = epoll_wait(ep_fd, ev_arr,MAX_EV_CNT, -1);
        for(int index = 0x00; index < cnt; index++)
        {
            if(ev_arr[index].data.fd == ser_fd)
            {
                int m_fd = lock(filename);
                if(m_fd <= 0x00)
                    continue;
                    
                bool loop = true;
                int get_cnt = 0x00;
                while(loop)
                {
                    if(get_cnt >= 10)
                        break;
                        
                    struct sockaddr_in cli_addr;
                    socklen_t len = 0x00;
                    int new_fd = accept(ser_fd, (struct sockaddr*)(&cli_addr),&len);
                    if(new_fd <= 0x00)
                    {
                        if(errno == EAGAIN || errno == EWOULDBLOCK)
                        {
                            loop = false;
                            continue;
                        }   
                        std::cout<<"accept failed ......."<<getpid()<<std::endl;
                    }
                    else
                    {
                        std::cout<<"accpet con is "<<new_fd<<std::endl;
                        
                        connection* obj = new connection;
                        memset(obj, 0x00, sizeof(*obj));
                        obj->fd = new_fd;
                        obj->left = 4096;
                        
                        set_noblock(new_fd);
                        struct epoll_event ev;
                        ev.data.ptr = obj;
                        ev.events = EPOLLIN | EPOLLET ;
                        
                        epoll_ctl(ep_fd, EPOLL_CTL_ADD, new_fd, &ev);
                        get_cnt++;
                    }
                }
                unlock(m_fd);
            }
            else 
            {
                if(ev_arr[index].events & EPOLLIN )
                {
                    connection* obj = (connection*)(ev_arr[index].data.ptr);
                    if(obj == NULL )
                    {
                        continue;
                    }
                
                    bool bloop = true;
                    bool breset = false;
                    int cli_fd = obj->fd;
                    while(bloop)
                    {
                        char buf[4096] = {0x00};
                        int recv_size = recv(cli_fd, buf, sizeof(buf), 0x00);
                        if(recv_size > 0x00)
                        {
                            int st = obj->left > recv_size ? recv_size : obj->left;
                            memcpy(obj->buf + obj->pos,buf,st);
                            obj->pos += st;
                            obj->left -= st;
                            if(recv_size < sizeof(buf))
                            {
                                bloop = false;
                            }
                        }   
                        else if(recv_size < 0x00)
                        {
                            if(errno == ECONNRESET)
                            {
                                breset = true;
                            }
                            else if(errno == EAGAIN)
                            {
                                bloop = false;
                            }
                        }
                        else if(recv_size == 0x00)
                        {
                            bloop = false;
                            breset = true;
                        }
                        
                        if(breset)
                        {
                            reset_con(obj,ep_fd,ev_arr,index);
                            bloop = false; 
                        }
                    }
                    if(!breset)
                    {
                        modify_con(obj,EPOLLOUT,ep_fd);
                    }
                }
                else if(ev_arr[index].events & EPOLLOUT)
                {
                    connection* obj = (connection*)(ev_arr[index].data.ptr);
                    if(obj == NULL)
                    {
                        continue;
                    }
                    
                    bool bloop = true;
                    bool breset = false;
                    int cli_fd = obj->fd;
                    while(bloop)
                    {
                        if(obj->pos <= 0x00)
                            break;
                            
                        int size = write(cli_fd,obj->buf,obj->pos);
                        if(size > 0x00)
                        {
                            if(size < obj->pos)
                            {
                                bloop = false;
                            }

                            for(int lp = 0x00; lp < obj->pos - size; lp++)
                                obj->buf[lp] = obj->buf[size + lp];
                                
                            obj->pos -= size;
                            obj->left += size;
                            
                        }
                        else if(size < 0x00 )
                        {
                            if(errno == ECONNRESET)
                            {
                                breset = true;
                            }
                            else if(errno == EAGAIN)
                            {
                                bloop = false;
                            }
                        }
                        else if(size == 0x00)
                        {
                            breset = true;
                        }
                        
                        if(breset)
                        {
                            reset_con(obj,ep_fd,ev_arr,index);
                            bloop = false; 
                        }
                        
                    }
                    if(!breset)
                    {
                        modify_con(obj,EPOLLIN,ep_fd);
                    }
                }
            }           
        }
    }
}


void show()
{
    for(std::map<int,int>::iterator it = ve.begin(); it != ve.end(); it++)
        std::cout<< it->first<<"   "<<it->second<<std::endl;
    
    if(ve.empty())
        std::cout<<"empty......"<<std::endl;
}

void fork_child(int num,int fd)
{
    for(int index = 0x00; index < num; index++)
    {
        int channel[2] = {0x00};
        char buf[10] = {0x00};
        if(socketpair(AF_UNIX,SOCK_STREAM,0,channel) == -1)
        {
            perror("scoket pair failed ......");
            continue;
        }
        
        pid_t id = fork();
        if(id == 0x00)
        {
            close(channel[1]);
            sprintf(buf,"%d",getpid());
            write(channel[0] , buf , strlen(buf));
            work(fd);
            return ;
        }   
        if(id < 0x00)
            std::cout<<" fork failed ....."<<std::endl;
            
        close(channel[0x00]);
        read(channel[1], buf, sizeof(buf));
        std::cout<<"child pid is "<<buf<<std::endl;
        close(channel[0x01]);
        ve[atoi(buf)] = SER_PORT;
        //SER_PORT++;
    }

}

int main()
{
    create_lkfile();
    SER_PORT = 6000;
    int ser_fd = pre_work();
    if(-1 == ser_fd)
    {
        std::cout<<"pre_work faild ...."<<std::endl;
        exit(0x01);
    }
    fork_child(0x04,ser_fd);
    sleep(1);
    show(); 
    while(1)
    {
        int status = 0x00;
        pid_t id = wait(&status);
        std::map<int,int>::iterator it = ve.find(id);
        if(it != ve.end())
        {
            SER_PORT = it->second;
            ve.erase(it);
        }
        
        fork_child(0x01,ser_fd);
        show();
        std::cout<<id<<"---over ......"<<std::endl;
    }
    return 0x00;
}