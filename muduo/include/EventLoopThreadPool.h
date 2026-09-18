#pragma once

#include <string>
#include <memory>
#include <functional>
#include <vector>
class EventLoop;
class EventLoopThread;

class EventLoopThreadPool
{
public:
    using ThreadInitCallBack = std::function<void(EventLoop*)>;
    EventLoopThreadPool(EventLoop* baseloop,const std::string& name);
    ~EventLoopThreadPool();

    void setThreadNum(int numThreads){numThreads_ = numThreads;}
    bool started() const {return started_;}
    const std::string getName() const {return name_;}
    std::vector<EventLoop*> getAllEventLoop();
    EventLoop* getNextLoop();
    void start(const ThreadInitCallBack& func = ThreadInitCallBack());
private:
    EventLoop* baseloop_; //用户设置的base loop
    std::string name_; //线程池的名字
    int numThreads_; //线程池的线程数量
    int next_; //用来轮询的下一个线程号
    bool started_; //开始标识
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;
};
