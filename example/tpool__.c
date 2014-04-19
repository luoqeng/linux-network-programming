/********************************** 
 * @author     <a href="mailto:wallwind@yeah.net">wallwind@<span style="color:#000000;">yeah.net</span></a>
 * @date        2012/06/13
 * Last update: 2012/06/13
 * License:     LGPL
 * 
 **********************************/
 
 #ifndef _GLOBAL_H_
 #define _GLOBAL_H_
 
 #include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>             /* */
#include <stdarg.h>
#include <stddef.h>             /* offsetof() */
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <glob.h>
#include <sys/vfs.h>            /* statfs() */

#include <sys/uio.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sched.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>        /* TCP_NODELAY, TCP_CORK */
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/un.h>

#include <time.h>               /* tzset() */
#include <malloc.h>             /* memalign() */
#include <limits.h>             /* IOV_MAX */
#include <sys/ioctl.h>
#include <sys/sysctl.h>
#include <crypt.h>
#include <sys/utsname.h>        /* uname() */
#include <semaphore.h>

#include <sys/epoll.h>
#include <poll.h>
#include <sys/syscall.h>
#include <pthread.h>
 #endif

 /********************************** 
 * @author      wallwind@yeah.net
 * @date        2012/06/13
 * Last update: 2012/06/13
 * License:     LGPL
 * 
 **********************************/
 
 
 
 #ifndef _THPOOL_
 #define _THPOOL_
 
 #include "global.h"
 /**
    定义一个任务节点
 **/
 typedef void* (*FUNC)(void* arg);
 
 
 typedef struct _thpool_job_t{
//  void* (*function)(void* arg);    //函数指针
    FUNC             function;
    void*                   arg;     //函数参数。
    struct _thpool_job_t* prev;     // 指向上一个节点
    struct _thpool_job_t* next;     //指向下一个节点
 } thpool_job_t;
 
 /**
    定义一个工作队列
 **/
 
typedef struct _thpool_job_queue{
    thpool_job_t*    head;            //队列头指针 
    thpool_job_t*    tail;             // 队列末尾指针
    int              jobN;                    //任务数
    sem_t*           queueSem;            //x信号量
}thpool_jobqueue; 
 
 /**
    线程池
 **/
 
 typedef struct _thpool_t{
    pthread_t*      threads;    ////线程指针数
    int             threadsN;    //// 线程数
    thpool_jobqueue* jobqueue;   // 指向队列指针
 }thpool_t;
 
 typedef struct thread_data{                            
    pthread_mutex_t *mutex_p;
    thpool_t        *tp_p;
}thread_data;

 //初始化线程池内部的线程数
thpool_t*  thpool_init(int threadN);

void thpool_thread_do(thpool_t* tp_p);

int thpool_add_work(thpool_t* tp_p, void *(*function_p)(void*), void* arg_p);

void thpool_destroy(thpool_t* tp_p);



int thpool_jobqueue_init(thpool_t* tp_p);



void thpool_jobqueue_add(thpool_t* tp_p, thpool_job_t* newjob_p);

int thpool_jobqueue_removelast(thpool_t* tp_p);

thpool_job_t* thpool_jobqueue_peek(thpool_t* tp_p);

void thpool_jobqueue_empty(thpool_t* tp_p);

 #endif

 /********************************** 
 * @author     wallwind@yeah.net
 * @date        2012/06/13
 * Last update: 2012/06/13
 * License:     LGPL
 * 
 **********************************/
 #include "global.h"
 #include "Thread.h"
 #include <errno.h>
 
 static int thpool_keepalive = 1;
 
 /* 创建互斥量，并初始化 */
  pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; /* used to serialize queue access */
 
thpool_t*  thpool_init(int threadN)
{
    thpool_t* thpool;
    if(!threadN || threadN < 1)
        threadN = 1;
    ///分配线程池内存
    thpool = (thpool_t*) malloc(sizeof(thpool_t));
    if(thpool ==NULL)
    {
        printf("malloc thpool_t error");
        return NULL;
    }
    //分配线程数
    thpool->threadsN = threadN;
    thpool->threads =(pthread_t*) malloc(threadN*sizeof(pthread_t));
    if(thpool->threads == NULL)
    {
        printf("malloc thpool->threads error");
        return NULL;
    }
    if(thpool_jobqueue_init(thpool))
        return -1;

    thpool->jobqueue->queueSem =(sem_t*)malloc(sizeof(sem_t));
    sem_init(thpool->jobqueue->queueSem,0,1);
    int t;
    for(t = 0;t< threadN ;t++)
    {
        pthread_create(&(thpool->threads[t]),NULL,(void *)thpool_thread_do,(void*)thpool);
    }
    
    return thpool;
}

void thpool_destroy(thpool_t* tp_p)
{
    int i ;
    thpool_keepalive = 0;
    
    for(i = 0;i < (tp_p->threadsN); i++)
    {
        if(sem_post(tp_p->jobqueue->queueSem))
        {
            fprintf(stderr, "thpool_destroy(): Could not bypass sem_wait()\n");
        }

    }
    if(sem_post(tp_p->jobqueue->queueSem)!=0)
    {
        fprintf(stderr, "thpool_destroy(): Could not destroy semaphore\n");
    }
    for(i = 0;i < (tp_p->threadsN); i++)
    {
        pthread_join(tp_p->threads[i],NULL);
    }
    thpool_jobqueue_empty(tp_p);
    
    free(tp_p->threads);
    free(tp_p->jobqueue->queueSem);
    free(tp_p->jobqueue);
    free (tp_p);
    
}
////对双向队列初始化
/* Initialise queue */
int thpool_jobqueue_init(thpool_t* tp_p){
    tp_p->jobqueue=(thpool_jobqueue*)malloc(sizeof(thpool_jobqueue));      /* MALLOC job queue */
    if (tp_p->jobqueue==NULL) return -1;
    tp_p->jobqueue->tail=NULL;
    tp_p->jobqueue->head=NULL;
    tp_p->jobqueue->jobN=0;
    return 0;
}

////
void thpool_thread_do(thpool_t* tp_p)
{
    while(thpool_keepalive ==1)
    {
        if(sem_wait(tp_p->jobqueue->queueSem)) ///线程阻塞,等待通知 直到消息队列有数据
        {
            perror("thpool_thread_do(): Waiting for semaphore");
            exit(1);
        }
        if(thpool_keepalive)
        {
            //(void*)(*function)(void *arg);
            FUNC function;
            void* arg_buff;
            thpool_job_t*  job_p;
            
            pthread_mutex_lock(&mutex);
             job_p = thpool_jobqueue_peek(tp_p);
            function = job_p->function;
            arg_buff = job_p->arg;
            if(thpool_jobqueue_removelast(tp_p))
                return ;
            pthread_mutex_unlock(&mutex);
            function(arg_buff);   //运行 你的方法。
            free(job_p);         ////释放掉。
        }
        else
        {
            return ;
        }
            
    }
    return ;
}

//得到第一个队列的一个节点
thpool_job_t* thpool_jobqueue_peek(thpool_t* tp_p)
{
    return tp_p->jobqueue->tail;
}
/////删除队列的最后一个节点
int thpool_jobqueue_removelast(thpool_t* tp_p)
{
    if(tp_p ==NULL)
        return -1;
    thpool_job_t* theLastJob;
    theLastJob = tp_p->jobqueue->tail;
    switch(tp_p->jobqueue->jobN)
    {
        case 0:
            return -1;
        case 1:
            tp_p->jobqueue->head =NULL;
            tp_p->jobqueue->tail =NULL;
            break;
        default:
            theLastJob->prev->next = NULL;
            tp_p->jobqueue->tail = theLastJob->prev;
                
    }
    (tp_p->jobqueue->jobN)--;
    int reval;
    sem_getvalue(tp_p->jobqueue->queueSem,&reval);
    return 0;   
}

void thpool_jobqueue_add(thpool_t* tp_p, thpool_job_t* newjob_p)
{
    newjob_p->next = NULL;
    newjob_p->prev = NULL;
    thpool_job_t* oldFirstJob;
    oldFirstJob = tp_p->jobqueue->head;
    
    switch(tp_p->jobqueue->jobN)
    {
        case 0:
            tp_p->jobqueue->head = newjob_p;
            tp_p->jobqueue->tail = newjob_p;
            break;
        default:
            oldFirstJob->prev = newjob_p;
            newjob_p->next = oldFirstJob;
            tp_p->jobqueue->head = newjob_p;
    
    }
    (tp_p->jobqueue->jobN)++;
    sem_post(tp_p->jobqueue->queueSem);
    
    int reval;
    sem_getvalue(tp_p->jobqueue->queueSem,&reval);
    return;
}

/////将消息加入线程池
int thpool_add_work(thpool_t* tp_p, void* (*function_p)(void*), void* arg_p)
{
    thpool_job_t* newjob;
    newjob = (thpool_job_t*) malloc(sizeof(thpool_job_t));
    
    if(newjob ==NULL)
    {
        fprintf(stderr, "thpool_add_work(): Could not allocate memory for new job\n");
        exit(1);
    }
    newjob ->function = function_p;
    newjob ->arg      = arg_p;
    pthread_mutex_lock(&mutex);
    thpool_jobqueue_add(tp_p,newjob);
    pthread_mutex_unlock(&mutex);     
    return 0;
}

///清空队列
void thpool_jobqueue_empty(thpool_t* tp_p)
{
    thpool_job_t* curjob;
    curjob = tp_p->jobqueue->tail;
    
    while(tp_p->jobqueue->jobN)
    {
        tp_p->jobqueue->tail = curjob->prev;
        free (curjob);
        curjob = tp_p->jobqueue->tail;
        (tp_p->jobqueue->jobN)--;
    }
    tp_p->jobqueue->head = NULL;
    tp_p->jobqueue->tail = NULL;
}

/********************************** 
 * @author      wallwind@yeah.net
 * @date        2012/06/13
 * Last update: 2012/06/13
 * License:     LGPL
 * 
 **********************************/

#include "global.h"
#include "Thread.h"

    void* task1()
    {
        printf("# Thread working: %u\n", (int)pthread_self());
        printf("  Task 1 running..\n");
    }


    /* Some arbitrary task 2 */
    void* task2(int a)
    {
        printf("# Thread working: %u\n", (int)pthread_self());
        printf("  Task 2 running..\n");
        printf("%d\n", a);
    }
int main()
{
    printf("~~~~~~~~~~~");
    thpool_t* thpool;
    int i;
    thpool = thpool_init(5);
    puts("Adding 20 tasks to threadpool");
    int a=54;
    for (i=0; i<20; i++){
        thpool_add_work(thpool, (void*)task1, NULL);
        thpool_add_work(thpool, (void*)task2, (void*)a);
    };


    puts("Will kill threadpool");
    thpool_destroy(thpool);
    
}