#include "EventLoopThreadPool.h"
#include "EventLoop.h"
#include "EventLoopThread.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseloop,
    const std::string& name)
    :baseloop_(baseloop),
    name_(name),
    numThreads_(0),
    next_(0),
    started_(false)
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // Don't delete loop, it's stack variable
}

void EventLoopThreadPool::start(const ThreadInitCallBack& func){
    started_ = true;
    for(int i=0;i<numThreads_;i++){
        std::string threadName = name_ + std::to_string(i);
        EventLoopThread* t = new EventLoopThread(func, threadName);
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));
        loops_.push_back(t->startLoop()); //创建出底层的线程，返回loop指针
    }
    if(numThreads_==0&&func){
        func(baseloop_);
    }
}
/**
 * 如果没有设置多个线程，也就没有subEventLoop，每一次分配任务都是返回baseloop
 * 如果设置的多个线程，那么会以轮询的方式循环分配channel
 */
EventLoop* EventLoopThreadPool::getNextLoop(){
    EventLoop* loop = baseloop_;
    if(!loops_.empty()){
        loop = loops_[next_++];
        if(next_>=loops_.size()) next_ = 0;
    }
    return loop;
}

std::vector<EventLoop*> EventLoopThreadPool::getAllEventLoop(){
    if(loops_.empty()){
        return std::vector<EventLoop*>(1,baseloop_);
    }
    else{
        return loops_;
    }
}

