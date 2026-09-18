#include "EventLoopThread.h"
#include "EventLoop.h"

EventLoopThread::EventLoopThread(const ThreadInitCallBack& func,
                const std::string& name)
    :loop_(nullptr),
    exiting_(false),
    mutex_(),
    cond_(),
    thread_([this]{initFunc();},name),
    threadInitCallBack_(func)
{
}

EventLoopThread::~EventLoopThread(){
    exiting_ = true;
    if(loop_!=nullptr){
        loop_->quit();
        thread_.join();
    }
}

EventLoop* EventLoopThread::startLoop(){
    thread_.start();
    EventLoop* loop = nullptr;
    {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_.wait(lock,[this](){return loop_!=nullptr;});
        loop = loop_;
    }
    return loop;
}

void EventLoopThread::initFunc(){
    EventLoop loop;

    if(threadInitCallBack_){
        threadInitCallBack_(&loop);
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one();
    }
    loop.loop();
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr;
}


