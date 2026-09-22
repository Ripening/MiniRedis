#pragma once

#include "noncopyable.h"
#include "Timer.h"
#include "TimerId.h"
#include <memory>
#include <set>
#include <utility>
#include <vector>

class EventLoop;
class Channel;

// 定时器队列:set 是按到期时刻排好的"排队清单",timerfd 让内核到点叫醒 eventloop。
// 无锁:所有操作只允许在所属 loop 线程执行,公开入口用 runInLoop 兜底转发。
class TimerQueue : public noncopyable
{
public:
    explicit TimerQueue(EventLoop* loop);
    ~TimerQueue();

    TimerId addTimer(TimerCallback cb,Timestamp when,double intervalSeconds);
    void cancel(TimerId timerId);

private:
    using Entry = std::pair<Timestamp,Timer*>;  // 同一时刻用指针地址兜底排序
    using TimerList = std::set<Entry>;

    void addTimerInLoop(Timer* timer);
    void cancelInLoop(TimerId timerId);
    void handleRead();                      // timerfd 可读 → 执行到期定时器
    std::vector<Entry> getExpired(Timestamp now);
    void resetTimerfd(Timestamp when);      // 把"下次几点叫我"告诉内核

    EventLoop* loop_;
    const int timerfd_;
    std::unique_ptr<Channel> timerChannel_;
    TimerList timers_;
};
