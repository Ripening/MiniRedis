#pragma once
#include <memory>
#include <vector>
#include <atomic>
#include <functional>
#include <mutex>
#include "CurrentThread.h"
#include "noncopyable.h"
class Channel;
class Poller;

class EventLoop : public noncopyable
{
public:
    using Functor = std::function<void()>;
    EventLoop();
    ~EventLoop();

    void loop();
    void quit();
    void runInLoop(Functor cb);

    void updateChannel(Channel* channel);
    void removeChannel(Channel* channel);
    bool hasChannel(Channel* channel);
    
    bool isInLoopThread(){return threadPid_ == CurrentThread::tid();}
    void queueInLoop(Functor cb);
    private:
    void handleRead();
    void wakeup();
    
    void doingPendingFunctor();
   
private:
    std::unique_ptr<Poller> poll_; //保存poll
    std::unique_ptr<Channel> wakeupChannel_; //唤醒的Chanel
    int wakeupEventFd_; //用于唤醒线程的fd
    const pid_t threadPid_;  //用于保存当前的线程号
    std::mutex mutex_;

    std::atomic<bool> looping_; //正在运行
    std::atomic<bool> quit_;    //停止
    std::atomic<bool> callingPendingFuncors_;   //正在执行待处理的函数

    using ChannelList = std::vector<Channel*>;
    ChannelList activeChannels_;    //保存准备好的channel
    std::vector<Functor> pendingFunctions_;
};
