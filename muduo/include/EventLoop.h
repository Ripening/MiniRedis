#pragma once
#include <memory>
#include <vector>
#include <atomic>
#include <functional>
#include <mutex>
#include "CurrentThread.h"
#include "noncopyable.h"
#include "Timer.h"
#include "TimerId.h"
class Channel;
class Poller;
class TimerQueue;

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

    // 定时器接口(线程安全:内部经 runInLoop 转发给 TimerQueue)
    TimerId runAt(Timestamp when,TimerCallback cb);
    TimerId runAfter(double delaySeconds,TimerCallback cb);
    TimerId runEvery(double intervalSeconds,TimerCallback cb);
    void cancel(TimerId timerId);
    private:
    void handleRead();
    void wakeup();
    
    void doingPendingFunctor();
   
private:
    std::unique_ptr<Poller> poll_; //保存poll
    std::unique_ptr<TimerQueue> timerQueue_; //定时器队列
    int wakeupEventFd_; //用于唤醒线程的fd
    // 成员按声明顺序初始化:wakeupEventFd_ 必须声明在 wakeupChannel_ 之前,
    // 否则 Channel 构造时读到的是未初始化的 fd,唤醒 channel 会注册失败
    std::unique_ptr<Channel> wakeupChannel_; //唤醒的Chanel
    const pid_t threadPid_;  //用于保存当前的线程号
    std::mutex mutex_;

    std::atomic<bool> looping_; //正在运行
    std::atomic<bool> quit_;    //停止
    std::atomic<bool> callingPendingFuncors_;   //正在执行待处理的函数

    using ChannelList = std::vector<Channel*>;
    ChannelList activeChannels_;    //保存准备好的channel
    std::vector<Functor> pendingFunctions_;
};
