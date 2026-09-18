#include "Thread.h"
#include "CurrentThread.h"

#include <semaphore.h>

Thread::Thread(ThreadFunc func,const std::string& name)
    :threadFunc_(std::move(func)),
    name_(name),
    started_(false),
    joined_(false),
    tid_(0)
{
    setDefaultName(); 
}

void Thread::setDefaultName(){
    int number = ++numCreated_;
    char buf[64]={0};
    if(name_.empty()){
        snprintf(buf,sizeof(buf),"Thread%d",number);
        name_ = buf;
    }
}

Thread::~Thread()
{
    if (started_ && !joined_)
    {
        thread_->detach();                                                  // thread类提供了设置分离线程的方法 线程运行后自动销毁（非阻塞）
    }
}

void Thread::join()
{
    joined_ = true;
    thread_->join();
}

void Thread::start(){
    started_ = true;
    sem_t sem;
    sem_init(&sem,false,0);
    thread_ = std::make_shared<std::thread>([&](){
        tid_ = CurrentThread::tid();
        sem_post(&sem);
        threadFunc_();
    });
    sem_wait(&sem);
}