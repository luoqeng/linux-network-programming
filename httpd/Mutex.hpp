#include<iostream>
#include<string>
#include<unistd.h>
#include<pthread.h>
#include<assert.h>
#include<boost/utility.hpp>
using namespace std;
using namespace boost;
/*
*互斥量
*/
class Mutex:noncopyable{
    public:
        Mutex(){
            pthread_mutex_init(&mutex,NULL);
        }
        void lock(){
            pthread_mutex_lock(&mutex);
        }
        void unlock(){
            pthread_mutex_unlock(&mutex);
        }
        pthread_mutex_t& get(){
            return mutex;
        }
    private:
        pthread_mutex_t mutex;
};
/*
*互斥量RAII
*/
class MutexLockGuard:noncopyable{
    public:
        explicit MutexLockGuard(Mutex& mutex):mutex_(mutex){
            mutex_.lock();
        }
        ~MutexLockGuard(){
            mutex_.unlock();
        }
    private:
        Mutex& mutex_;
};
/*
*条件变量
*/
class Condition:noncopyable{
    public:
        explicit Condition(Mutex& mutex):mutex_(mutex){
            pthread_cond_init(&pcond_,NULL);
        }
        ~Condition(){
            pthread_cond_destroy(&pcond_);
        }
        void wait(){
            pthread_cond_wait(&pcond_,&mutex_.get());
        }
        void notify(){
            pthread_cond_signal(&pcond_);
        }
        void notifyALL(){
            pthread_cond_broadcast(&pcond_);
        }
    private:
        Mutex& mutex_;
        pthread_cond_t pcond_;
};
/*
*倒计时闩
*/
class CountDownLatch{
    public:
        CountDownLatch(int count):mutex_(),condition_(mutex_),count_(count){}
        void wait(){
            MutexLockGuard lock(mutex_);
            while(count_>0)
                condition_.wait();
        }
        void countDown(){
            MutexLockGuard lock(mutex_);
            --count_;
            if(count_==0)
                condition_.notifyALL();
        }
    private:
        mutable Mutex mutex_;
        Condition condition_;
        int count_;
};



#include<iostream>
#include<pthread.h>
#include<unistd.h>
#include<boost/shared_ptr.hpp>
#include<boost/weak_ptr.hpp>
#include<boost/noncopyable.hpp>
#include<boost/function.hpp>
#include<boost/bind.hpp>
#include<boost/ptr_container/ptr_vector.hpp>
#include<string>
#include<deque>
#include<algorithm>
#include<sys/syscall.h>
#include<stdio.h>
#include<assert.h>
#include"Mutex.hpp"
using namespace std;
using namespace boost;
/*
 *线程类Thread
 */
__thread pid_t t_cacheTid=0;//线程私有数据线程ID避免通过系统调用获得ID
class Thread:noncopyable{
    public:
        typedef function<void()> ThreadFunc;//线程需要执行工作函数
        explicit Thread(const ThreadFunc& a,const string& name=string()):started_(false),
            joinded_(false),pthreadID_(0),tid_(new pid_t(0)),func_(a),name_(name){
            }
        ~Thread(){
            if(started_&&!joinded_){
                pthread_detach(pthreadID_);//分离线程
            }
        }
        void start();
        /*
        {
            assert(!started_);
            started_=true;
            if(pthread_create(&pthreadID_,NULL,&startThread,NULL)){
                started_=false;
                abort();//终止进程刷新缓冲区
            }
        }
        *///###1###使用此处会出错详见http://cboard.cprogramming.com/cplusplus-programming/113981-passing-class-member-function-pthread_create.html
        void join(){//等待线程执行完工作函数
            assert(started_);
            assert(!joinded_);
            joinded_=true;
            pthread_join(pthreadID_,NULL);
        }
        pid_t tid() const{
            if(t_cacheTid==0){//如果没有缓存t_cacheTid则获取线程ID否则直接通过线程私有数据返回ID减少系统调用
                t_cacheTid=syscall(SYS_gettid);
            }
            return t_cacheTid;
        }
        const string& name() const{
            return name_;
        }
        //void* startThread(void* arg){//###1###
        void startThread(){
            func_();
        }
    private:
        bool started_;
        bool joinded_;
        pthread_t pthreadID_;
        shared_ptr<pid_t> tid_;
        ThreadFunc func_;
        string name_;
};
void* threadFun(void* arg){//采用间接层执行工作函数
    Thread* thread=static_cast<Thread*>(arg);
    thread->startThread();
}
void Thread::start(){
    assert(!started_);
    started_=true;
    if(pthread_create(&pthreadID_,NULL,threadFun,this)){
        started_=false;
        abort();//终止进程刷新缓冲区
    }
}

/*
 * 线程局部数据TSD
 */
template<typename T>
class ThreadLocal:noncopyable{
    public:
        ThreadLocal(){
            pthread_key_create(&pkey_,&destructor);//每个线程会设定自己的pkey_并在pthread_key_delete执行destructor操作
        }
        ~ThreadLocal(){
            pthread_key_delete(pkey_);//执行destructor操作
        }
        T& value(){//采用单件模式，此处不会跨线程使用故不存在非线程安全的singleton问题
            T* perThreadValue=static_cast<T*>(pthread_getspecific(pkey_));
            if(!perThreadValue){
                T* newObj=new T();
                pthread_setspecific(pkey_,newObj);
                perThreadValue=newObj;
            }
            return *perThreadValue;
        }
    private:
        static void destructor(void* x){//清除私有数据
            T* obj=static_cast<T*>(x);
            delete obj;
        }
    private:
        pthread_key_t pkey_;
};
/*
 * 线程池
 */
class ThreadPool:noncopyable{
    public:
        typedef function<void()> Task;//线程工作函数
        explicit ThreadPool(const string& name=string()):mutex_(),cond_(mutex_),name_(name),running_(false){
        }
        ~ThreadPool(){
            if(running_){
                stop();//等待所有线程池中的线程完成工作
            }
        }
        void start(int numThreads){
            assert(threads_.empty());
            running_=true;
            threads_.reserve(numThreads);
            for(int i=0;i<numThreads;i++){
                threads_.push_back(new Thread(bind(&ThreadPool::runInThread,this)));//池中线程运行runInThread工作函数
                threads_[i].start();
            }
        }
        void stop(){
            running_=false;//可以提醒使用者不要在此后添加任务了，因为停止池但是池还要等待池中线程完成任务
            cond_.notifyALL();//唤醒池中所有睡眠的线程
            for_each(threads_.begin(),threads_.end(),bind(&Thread::join,_1));//等待池中线程完成
        }
        void run(const Task& task){
            if(running_){//###4###防止停止池运行后还有任务加进来
                if(threads_.empty()){//池中没有线程
                    task();
                }
                else{
                    MutexLockGuard guard(mutex_);//使用RAII mutex保证线程安全
                    queue_.push_back(task);
                    cond_.notify();
                }
            }
            else{
                printf("线程池已停止运行\n");
            }
        }
        bool running(){//使用者可以获取线程池的运行状态
            return running_;
        }
    private:
        void runInThread(){//线程工作函数
            while(running_){//###2###
                Task task(take());
                if(task){//task可能意外的为NULL
                    task();
                }
            }
        }
        Task take(){
            MutexLockGuard guard(mutex_);
            while(queue_.empty()&&running_){//###3###和###2###不能保证在池停止运行但是线程还没有完成操作期间安全。假设此期间有任务添加到池中，且某个线程A执行到###2###后马上被切换了，池running_=false停止运行，A被切换后运行执行###3###处无意义啊，因为池已经停止运行了。所以###4###是有必要提醒使用者池停止这一情景
                cond_.wait();//池中没有任务等待
            }
            Task task;
            if(!queue_.empty()){
                task=queue_.front();
                queue_.pop_front();
            }
            return task;
        }
        Mutex mutex_;
        Condition cond_;
        string name_;
        ptr_vector<Thread> threads_;//智能指针容器
        deque<Task> queue_;
        bool running_;
};
/*
 *测试代码
 */
class test{
    public:
        test(){
            printf("test::constructor\n");
        }
        ~test(){
            printf("test::deconsturctor\n");
        }
};
void worker1(){
    ThreadLocal<test> one;
    test two=one.value();//线程局部数据会在线程结束时释放
    printf("worker1\n");
}
void worke2(){
    printf("worker2\n");
}
void worker3(){
    printf("worker3\n");
    sleep(1);
}
int main(){
    Thread one(worker1);
    one.start();
    one.join();
    ThreadPool two;
    two.start(2);
    two.run(worke2);
    two.run(worker3);
    two.stop();
    return 0;
}