++++++++++++++++++++++++++++++++++++++++++++++
threadpool.c
++++++++++++++++++++++++++++++++++++++++++++++
/**
* threadpool.c
*
* This file will contain your implementation of a threadpool.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>

#include "threadpool.h"

typedef struct _thread_st {
        pthread_t id;
        pthread_mutex_t mutex;
        pthread_cond_t cond;
        dispatch_fn fn;
        void *arg;
        threadpool parent;
} _thread;

// _threadpool is the internal threadpool structure that is
// cast to type "threadpool" before it given out to callers
typedef struct _threadpool_st {
        // you should fill in this structure with whatever you need
        pthread_mutex_t tp_mutex;
        pthread_cond_t tp_idle;
        pthread_cond_t tp_full;
        pthread_cond_t tp_empty;
        _thread ** tp_list;
        int tp_index;
        int tp_max_index;
        int tp_stop;

        int tp_total;
} _threadpool;

threadpool create_threadpool(int num_threads_in_pool)
{
        _threadpool *pool;

        // sanity check the argument
        if ((num_threads_in_pool <= 0) || (num_threads_in_pool > MAXT_IN_POOL))
                return NULL;

        pool = (_threadpool *) malloc(sizeof(_threadpool));
        if (pool == NULL) {
                fprintf(stderr, "Out of memory creating a new threadpool!\n");
                return NULL;
        }

        // add your code here to initialize the newly created threadpool
        pthread_mutex_init( &pool->tp_mutex, NULL );
        pthread_cond_init( &pool->tp_idle, NULL );
        pthread_cond_init( &pool->tp_full, NULL );
        pthread_cond_init( &pool->tp_empty, NULL );
        pool->tp_max_index = num_threads_in_pool;
        pool->tp_index = 0;
        pool->tp_stop = 0;
        pool->tp_total = 0;
        pool->tp_list = ( _thread ** )malloc( sizeof( void * ) * MAXT_IN_POOL );
        memset( pool->tp_list, 0, sizeof( void * ) * MAXT_IN_POOL );

        return (threadpool) pool;
}

int save_thread( _threadpool * pool, _thread * thread )
{
        int ret = -1;

        pthread_mutex_lock( &pool->tp_mutex );

        if( pool->tp_index < pool->tp_max_index ) {
                pool->tp_list[ pool->tp_index ] = thread;
                pool->tp_index++;
                ret = 0;

                pthread_cond_signal( &pool->tp_idle );

                if( pool->tp_index >= pool->tp_total ) {
                        pthread_cond_signal( &pool->tp_full );
                }
        }

        pthread_mutex_unlock( &pool->tp_mutex );

        return ret;
}

void * wrapper_fn( void * arg )
{
        _thread * thread = (_thread*)arg;
        _threadpool * pool = (_threadpool*)thread->parent;

        for( ; 0 == ((_threadpool*)thread->parent)->tp_stop; ) {
                thread->fn( thread->arg );

                pthread_mutex_lock( &thread->mutex );
                if( 0 == save_thread( thread->parent, thread ) ) {
                        pthread_cond_wait( &thread->cond, &thread->mutex );
                        pthread_mutex_unlock( &thread->mutex );
                } else {   /* 当前线程池中所有的线程都已经成为follower，没有空闲的地方让下一个线程来成为follower. 问题是这种情况在这个程序中感觉不会出现啊？？？  下面红色代码逻辑有用么？？*/
                        pthread_mutex_unlock( &thread->mutex ); 
                        pthread_cond_destroy( &thread->cond );
                        pthread_mutex_destroy( &thread->mutex );
                        free( thread );
                        break;
                }
        }

        pthread_mutex_lock( &pool->tp_mutex );
        pool->tp_total--;
        if( pool->tp_total <= 0 ) pthread_cond_signal( &pool->tp_empty );
        pthread_mutex_unlock( &pool->tp_mutex );

        return NULL;
}

int dispatch_threadpool(threadpool from_me, dispatch_fn dispatch_to_here, void *arg)
{
        int ret = 0;

        _threadpool *pool = (_threadpool *) from_me;
        pthread_attr_t attr;
        _thread * thread = NULL;

        // add your code here to dispatch a thread
        pthread_mutex_lock( &pool->tp_mutex );


        /*  当所有线程都是worker没有成为follower，并且当前已经生成的线程的个数 >= 存放follower的空间个数 
一直等待某一个worker线程变成follower,根据此处的逻辑，那上面的红色代码能有机会执行得到？？    */
        if( pool->tp_index <= 0 && pool->tp_total >= pool->tp_max_index ) {
                pthread_cond_wait( &pool->tp_idle, &pool->tp_mutex );
        }

        if( pool->tp_index <= 0 ) {
                _thread * thread = ( _thread * )malloc( sizeof( _thread ) );
                thread->id = 0;
                pthread_mutex_init( &thread->mutex, NULL );
                pthread_cond_init( &thread->cond, NULL );
                thread->fn = dispatch_to_here;
                thread->arg = arg;
                thread->parent = pool;

                pthread_attr_init( &attr );
                pthread_attr_setdetachstate( &attr,PTHREAD_CREATE_DETACHED );

                if( 0 == pthread_create( &thread->id, &attr, wrapper_fn, thread ) ) {
                        pool->tp_total++;
                        printf( "create thread#%ld\n", thread->id );
                } else {
                        ret = -1;
                        printf( "cannot create thread\n" );
                        pthread_mutex_destroy( &thread->mutex );
                        pthread_cond_destroy( &thread->cond );
                        free( thread );
                }
        } else {
                pool->tp_index--;
                thread = pool->tp_list[ pool->tp_index ];
                pool->tp_list[ pool->tp_index ] = NULL;

                thread->fn = dispatch_to_here;
                thread->arg = arg;
                thread->parent = pool;

                pthread_mutex_lock( &thread->mutex );
                pthread_cond_signal( &thread->cond ) ;
                pthread_mutex_unlock ( &thread->mutex );
        }

        pthread_mutex_unlock( &pool->tp_mutex );

        return ret;
}

void destroy_threadpool(threadpool destroyme)
{
        _threadpool *pool = (_threadpool *) destroyme;

        // add your code here to kill a threadpool
        int i = 0;

        pthread_mutex_lock( &pool->tp_mutex );

        if( pool->tp_index < pool->tp_total ) {
                printf( "waiting for %d thread(s) to finish\n", pool->tp_total - pool->tp_index );
                pthread_cond_wait( &pool->tp_full, &pool->tp_mutex );
        }

        pool->tp_stop = 1;

        for( i = 0; i < pool->tp_index; i++ ) {
                _thread * thread = pool->tp_list[ i ];

                pthread_mutex_lock( &thread->mutex );
                pthread_cond_signal( &thread->cond ) ;
                pthread_mutex_unlock ( &thread->mutex );
        }

        if( pool->tp_total > 0 ) {
                printf( "waiting for %d thread(s) to exit\n", pool->tp_total );
                pthread_cond_wait( &pool->tp_empty, &pool->tp_mutex );
        }

        for( i = 0; i < pool->tp_index; i++ ) {
                free( pool->tp_list[ i ] );
                pool->tp_list[ i ] = NULL;
        }

        pthread_mutex_unlock( &pool->tp_mutex );

        pool->tp_index = 0;

        pthread_mutex_destroy( &pool->tp_mutex );
        pthread_cond_destroy( &pool->tp_idle );
        pthread_cond_destroy( &pool->tp_full );
        pthread_cond_destroy( &pool->tp_empty );

        free( pool->tp_list );
        free( pool );
}

++++++++++++++++++++++++++++++++++++++++++++++
testthreadpool.c
++++++++++++++++++++++++++++++++++++++++++++++
/**
* threadpool_test.c, copyright 2001 Steve Gribble
*
* Just a regression test for the threadpool code.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <stdarg.h>
#include "threadpool.h"

extern int errno;

void mylog( FILE * fp, const  char  *format,  /*args*/ ...)
{
        va_list ltVaList;
        va_start( ltVaList, format );
        vprintf( format, ltVaList );
        va_end( ltVaList );

        fflush( stdout );
}

void dispatch_threadpool_to_me(void *arg) {
  int seconds = (int) arg;

  fprintf(stdout, "  in dispatch_threadpool %d\n", seconds);
  fprintf(stdout, "  thread#%ld\n", pthread_self() );
  sleep(seconds);
  fprintf(stdout, "  done dispatch_threadpool %d\n", seconds);
}

int main(int argc, char **argv) {
  threadpool tp;

  tp = create_threadpool(2);

  fprintf(stdout, "**main** dispatch_threadpool 3\n");
  dispatch_threadpool(tp, dispatch_threadpool_to_me, (void *) 3);
  fprintf(stdout, "**main** dispatch_threadpool 6\n");
  dispatch_threadpool(tp, dispatch_threadpool_to_me, (void *) 6);
  fprintf(stdout, "**main** dispatch_threadpool 7\n");
  dispatch_threadpool(tp, dispatch_threadpool_to_me, (void *) 7);

  fprintf(stdout, "**main** done first\n");
  sleep(20);
  fprintf(stdout, "\n\n");

  fprintf(stdout, "**main** dispatch_threadpool 3\n");
  dispatch_threadpool(tp, dispatch_threadpool_to_me, (void *) 3);
  fprintf(stdout, "**main** dispatch_threadpool 6\n");
  dispatch_threadpool(tp, dispatch_threadpool_to_me, (void *) 6);
  fprintf(stdout, "**main** dispatch_threadpool 7\n");
  dispatch_threadpool(tp, dispatch_threadpool_to_me, (void *) 7);

  fprintf(stdout, "**main done second\n");

  destroy_threadpool( tp );

  sleep(20);
  exit(-1);
}