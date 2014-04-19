//ptypes
#include "ptime.h"
#include "pinet.h"
#include "ptypes.h"
#include "pasync.h"
#include "logfile.h"

//network
#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <iostream>
#include <set>
using namespace std;
USING_PTYPES
#define MAX_EVENTS 10000
#define maxtoken 1024
const int maxthreads = 64;
const int MSG_MYJOB = 100;
#define BUFF_MAX 1 //现在不用到事件里的缓存区，先设定最小值
struct myevent_s
{
    int fd;
    void (*call_back)(int fd, int events, void *arg);
    int events;
    void *arg;
    int status; // 1: in epoll wait list, 0 not in
    char buff[BUFF_MAX]; // recv data buffer
    int len;
    long last_active; // last active time
};

//以Socket为关键字的集合.
std::set<int> m_set;
//增加锁定。
mutex m_mxLock;

//把socket插入到集合中,如果存在返回false
bool  InsertSocket(int nSocket)
{
    scopelock lock(m_mxLock);
    if (m_set.end() == m_set.find(nSocket))
    {
        //没有找到插入并返回false；
        m_set.insert(nSocket);
        return true;
    }
    else
    {
        //已经存在不插入返回false
        return false;
    }
}
//重集合中删除socket;
bool  EraseSocket(int nSocket)
{
    scopelock lock(m_mxLock);
    //如果集合中存在socket,把Socket从集合里删除
    if (m_set.end() != m_set.find(nSocket))
    {
        //找到再删除；
        m_set.erase(nSocket);
        return true;
    }
    else
    {
        //不存在不删除
        return false;
    }
}

// set event
void EventSet(myevent_s *ev, int fd, void (*call_back)(int, int, void*), void *arg)
{
    ev->fd = fd;
    ev->call_back = call_back;
    ev->events = 0;
    ev->arg = arg;
    ev->status = 0;
    ev->last_active = time(NULL);
}
// add/mod an event to epoll
void EventAdd(int epollFd, int events, myevent_s *ev)
{
    struct epoll_event epv = {0, {0}};
    int op;
    epv.data.ptr = ev;
    epv.events = ev->events = events;
    if(ev->status == 1){
        op = EPOLL_CTL_MOD;
    }
    else{
        op = EPOLL_CTL_ADD;
        ev->status = 1;
    }
    if(epoll_ctl(epollFd, op, ev->fd, &epv) < 0)
    {
        printf("Event Add failed[fd=%d], status:%d\n", ev->fd, ev->status);
        CLogFile::Instance()->LOG_WriteLine(LOG_INFO_ERROR, "%s: Event Add failed[fd=%d], status:%d error=%d %s\n",  __func__, ev->fd, ev->status,errno, strerror(errno));
    }
    else
    {
        printf("Event Add OK[fd=%d]\n", ev->fd);
        CLogFile::Instance()->LOG_WriteLine(LOG_INFO_HIT, "%s: Event Add OK[fd=%d]\n", __func__,  ev->fd);
    }
}
// delete an event from epoll
void EventDel(int epollFd, myevent_s *ev)
{
    struct epoll_event epv = {0, {0}};
    if(ev->status != 1) return;
    epv.data.ptr = ev;
    ev->status = 0;
    epoll_ctl(epollFd, EPOLL_CTL_DEL, ev->fd, &epv);
}
int g_epollFd;
myevent_s g_Events[MAX_EVENTS+1]; // g_Events[MAX_EVENTS] is used by listen fd
void RecvData(int fd, int events, void *arg);
void SendData(int fd, int events, void *arg);
// accept new connections from clients
void AcceptConn(int fd, int events, void *arg)
{
    struct sockaddr_in sin;
    socklen_t len = sizeof(struct sockaddr_in);
    int nfd, i;
    // accept
    if((nfd = accept(fd, (struct sockaddr*)&sin, &len)) == -1)
    {
        if(errno != EAGAIN && errno != EINTR)
        {
            printf("%s: bad accept", __func__);
            CLogFile::Instance()->LOG_WriteLine(LOG_INFO_ERROR, "%s: bad accept error = %d %s", __func__, errno, strerror(errno) );
        }
        return;
    }
    do
    {
        for(i = 0; i < MAX_EVENTS; i++)
        {
            if(g_Events[i].status == 0)
            {
                break;
            }
        }
        if(i == MAX_EVENTS)
        {
            printf("%s:max connection limit[%d].", __func__, MAX_EVENTS);
            CLogFile::Instance()->LOG_WriteLine(LOG_INFO_ERROR, "%s: max connection limit[%d]; error = %d %s", __func__, MAX_EVENTS, errno, strerror(errno) );
            break;
        }
        // set nonblocking
        if(fcntl(nfd, F_SETFL, O_NONBLOCK) < 0) break;
        // add a read event for receive data
        EventSet(&g_Events[i], nfd, RecvData, &g_Events[i]);
        EventAdd(g_epollFd, EPOLLIN|EPOLLET, &g_Events[i]);
        printf("new conn[%s:%d][time:%d]\n", inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), g_Events[i].last_active);
        CLogFile::Instance()->LOG_WriteLine(LOG_INFO_HIT, "%s: new conn[%s:%d][time:%d]", __func__, inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), g_Events[i].last_active);
    }while(0);
}
// receive data
void RecvData(int fd, int events, void *arg)
{
    struct myevent_s *ev = (struct myevent_s*)arg;
    int len;
    // receive data
    len = recv(fd, ev->buff, sizeof(ev->buff)-1, 0);    
    CLogFile::Instance()->LOG_WriteLine(LOG_INFO_HIT, "%s: recv len:%d", __func__, len);
    EventDel(g_epollFd, ev);
    if(len > 0)
    {
        ev->len = len;
        ev->buff[len] = '\0';
        printf("C[%d]:%s\n", fd, ev->buff);
        // change to send event
        EventSet(ev, fd, SendData, ev);
        EventAdd(g_epollFd, EPOLLOUT|EPOLLET, ev);
    }
    else if(len == 0)
    {
        close(ev->fd);
        printf("[fd=%d] closed gracefully.\n", fd);
        CLogFile::Instance()->LOG_WriteLine(LOG_INFO_HIT, "%s: closed gracefully", __func__);
    }
    else
    {
        close(ev->fd);
        printf("recv[fd=%d] error[%d]:%s\n", fd, errno, strerror(errno));
        CLogFile::Instance()->LOG_WriteLine(LOG_INFO_ERROR, "%s: recv[fd=%d] error[%d]:%s", __func__, fd, errno, strerror(errno));
    }
}
// send data
void SendData(int fd, int events, void *arg)
{
    struct myevent_s *ev = (struct myevent_s*)arg;
    int len;
    // send data
    len = send(fd, ev->buff, ev->len, 0);
    CLogFile::Instance()->LOG_WriteLine(LOG_INFO_HIT, "%s: send len:%d", __func__, len);
    ev->len = 0;
    EventDel(g_epollFd, ev);
    if(len > 0)
    {
        // change to receive event
        EventSet(ev, fd, RecvData, ev);
        EventAdd(g_epollFd, EPOLLIN|EPOLLET, ev);
    }
    else
    {
        close(ev->fd);
        printf("send[fd=%d] error[%d]\n", fd, errno);
        CLogFile::Instance()->LOG_WriteLine(LOG_INFO_HIT, " %s: send[fd=%d] error[%d]:%s", __func__, fd, errno, strerror(errno));
    }
}

void InitListenSocket(int epollFd, short port)
{
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    fcntl(listenFd, F_SETFL, O_NONBLOCK); // set non-blocking
    printf("server listen fd=%d/n", listenFd);
    EventSet(&g_Events[MAX_EVENTS], listenFd, AcceptConn, &g_Events[MAX_EVENTS]);
    // add listen socket
    //EventAdd(epollFd, EPOLLIN|EPOLLET, &g_Events[MAX_EVENTS]);
    EventAdd(epollFd, EPOLLIN, &g_Events[MAX_EVENTS]);
    // bind & listen
    sockaddr_in sin;
    bzero(&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);
    bind(listenFd, (const sockaddr*)&sin, sizeof(sin));
    listen(listenFd, SOMAXCONN);
}

//线程池函数
class myjobthread: public thread
{
protected:
    int id;
    jobqueue* jq;
    virtual void execute();
public:
    myjobthread(int iid, jobqueue* ijq)
        : thread(false), id(iid), jq(ijq)  {}
    ~myjobthread()  { waitfor(); }
};

class myjob: public message
{
public:
    myevent_s* m_pSocketInfo;
    myjob(myevent_s* pSocketInfo)
        : message(MSG_MYJOB), m_pSocketInfo(pSocketInfo)  {}
    ~myjob()  
    {
        //delete m_pSocketInfo;
    }
};


void myjobthread::execute()
{
    bool quit = false;
    while (!quit)
    {
        // get the next message from the queue
        message* msg = jq->getmessage();

        try
        {
            switch (msg->id)
            {
            case MSG_MYJOB:
                {
                    pout.putf("%t,线程：%d\n", now(), id);
                    

                    myevent_s *ev = ((myjob*)msg)->m_pSocketInfo;
                    if(ev->events&EPOLLIN) // read event
                    {
                        ev->call_back(ev->fd, ev->events, ev->arg);
                    }
                    if(ev->events&EPOLLOUT) // write event
                    {
                        ev->call_back(ev->fd, ev->events, ev->arg);
                    }


                    //pout.putf("%t,%d返回:%d字节\n",now(), id, nSend);

                }
                break;

            case MSG_QUIT:
                // MSG_QUIT is not used in our example
                quit = true;
                break;
            }
        }
        catch(pt::exception*)
        {
            // the message object must be freed!
            delete msg;
            throw;
        }
        delete msg;
    }
}

int main(int argc, char **argv)
{
    short port = 8108; // default port
    if(argc == 2){
        port = atoi(argv[1]);
    }

    CLogFile::Instance()->LOG_Start("./log/");

    //启动线程池
    jobqueue jq;
    tobjlist<myjobthread> threads(true);

    // create the thread pool
    for(int n = 0; n < maxthreads; n++)
    {
        myjobthread* j = new myjobthread(n + 1, &jq);
        j->start();
        threads.add(j);
    }

    // create epoll
    g_epollFd = epoll_create(MAX_EVENTS);
    if(g_epollFd <= 0) 
    {
        CLogFile::Instance()->LOG_WriteLine(LOG_INFO_ERROR, "create epoll failed.");
        printf("create epoll failed.%d\n", g_epollFd);
    }
    // create & bind listen socket, and add to epoll, set non-blocking
    InitListenSocket(g_epollFd, port);
    // event loop
    struct epoll_event events[MAX_EVENTS];
    printf("server running:port[%d]\n", port);
    int checkPos = 0;
    while(1)
    {
        // a simple timeout check here, every time 100, better to use a mini-heap, and add timer event
        long now = time(NULL);
        for(int k = 0; k < MAX_EVENTS; k++, checkPos++) // doesn't check listen fd
        {
            if(checkPos == MAX_EVENTS) checkPos = 0; // recycle
            if(g_Events[checkPos].status != 1) continue;
            long duration = now - g_Events[checkPos].last_active;
            if(duration >= 600) // 600s timeout
            {
                closesocket(g_Events[checkPos].fd);
                printf("[fd=%d] timeout[%d--%d].\n", g_Events[checkPos].fd, g_Events[checkPos].last_active, now);
                CLogFile::Instance()->LOG_WriteLine(LOG_INFO_WARN, "超时Socket：[fd=%d] timeout[%d--%d].", g_Events[checkPos].fd, g_Events[checkPos].last_active, now);
                EventDel(g_epollFd, &g_Events[checkPos]);
            }
        }
        // wait for events to happen

        int fds = epoll_wait(g_epollFd, events, MAX_EVENTS, -1);

        if(fds < 0)
        {
            printf("epoll_wait error, exit/n");
            CLogFile::Instance()->LOG_WriteLine(LOG_INFO_ERROR, "epoll_wait error.");
            break;
        }
        CLogFile::Instance()->LOG_WriteLine(LOG_INFO_HIT, "EPOLL_WAIT FDS counts:%d", fds);
        for(int i = 0; i < fds; i++)
        {
            myevent_s *ev = (struct myevent_s*)events[i].data.ptr;
            jq.post(new myjob(ev));

        }
    }
    // free resource
    return 0;
}
