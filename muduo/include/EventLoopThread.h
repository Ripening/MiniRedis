#pragma once

#include <functional>
#include <string>
#include <mutex>
#include <condition_variable>
#include "Thread.h"
#include "noncopyable.h"

class EventLoop;
class EventLoopThread:public noncopyable
{ 
public:
    using ThreadInitCallBack = std::function<void(EventLoop*)>;
    EventLoopThread(const ThreadInitCallBack& func = ThreadInitCallBack(),
                    const std::string& name = std::string());
    ~EventLoopThread();
    EventLoop* startLoop();
private:
    void initFunc();
    bool exiting_;
    EventLoop* loop_;
    Thread thread_;
    ThreadInitCallBack threadInitCallBack_;
    std::mutex mutex_; //配合条件变量使用
    std::condition_variable cond_; //条件变量
};

