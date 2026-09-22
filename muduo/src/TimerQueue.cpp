#include "TimerQueue.h"
#include "Channel.h"
#include "EventLoop.h"
#include <sys/timerfd.h>
#include <unistd.h>
#include <chrono>
#include <cstdint>

namespace{
    int createTimerfd(){
        const int fd = ::timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK|TFD_CLOEXEC);
        if(fd < 0){
            //log
        }
        return fd;
    }
}

TimerQueue::TimerQueue(EventLoop* loop)
    :loop_(loop),
    timerfd_(createTimerfd()),
    timerChannel_(new Channel(loop,timerfd_)),
    timers_()
{
    timerChannel_->setReadEvent([this]()->void{
        handleRead();
    });
    timerChannel_->enableReading();
}

TimerQueue::~TimerQueue(){
    timerChannel_->disableAll();
    timerChannel_->remove();
    ::close(timerfd_);
    for(const Entry& entry:timers_){
        delete entry.second;
    }
}

TimerId TimerQueue::addTimer(TimerCallback cb,Timestamp when,double intervalSeconds){
    Timer* timer = new Timer(std::move(cb),when,intervalSeconds);
    // 已经在 loop 线程就直接执行,否则排队 + 唤醒
    loop_->runInLoop([this,timer]()->void{
        addTimerInLoop(timer);
    });
    return TimerId(timer,timer->sequence());
}

void TimerQueue::addTimerInLoop(Timer* timer){
    // 插入前判断:新定时器是否会成为最早到期的
    const bool earliestChanged =
        timers_.empty() || timer->expiration() < timers_.begin()->first;
    timers_.insert(Entry(timer->expiration(),timer));
    if(earliestChanged){
        resetTimerfd(timer->expiration());
    }
}

void TimerQueue::cancel(TimerId timerId){
    loop_->runInLoop([this,timerId]()->void{
        cancelInLoop(timerId);
    });
}

void TimerQueue::cancelInLoop(TimerId timerId){
    // 指针先比(不解引用,悬垂指针也安全),序号再核对(地址被复用时识别出旧句柄)
    for(auto it = timers_.begin();it != timers_.end();++it){
        if(it->second == timerId.timer_){
            if(it->second->sequence() == timerId.sequence_){
                delete it->second;
                timers_.erase(it);
            }
            return;
        }
    }
}

void TimerQueue::handleRead(){
    uint64_t howmany = 0;
    const ssize_t n = ::read(timerfd_,&howmany,sizeof(howmany));
    if(n != static_cast<ssize_t>(sizeof(howmany))){
        //log
    }

    const Timestamp now = nowTimestamp();
    // 先把到期的都取出来再执行:回调里改动 timers_ 不影响这一批
    const std::vector<Entry> expired = getExpired(now);

    for(const Entry& entry:expired){
        entry.second->run();
    }
    // 重复的重算下次时刻插回,一次性的删掉
    for(const Entry& entry:expired){
        Timer* timer = entry.second;
        if(timer->repeat()){
            timer->restart(now);
            timers_.insert(Entry(timer->expiration(),timer));
        }else{
            delete timer;
        }
    }

    // 重设内核闹钟到剩下的最早时刻;清单空了就不管(最多再空醒一次)
    if(!timers_.empty()){
        resetTimerfd(timers_.begin()->first);
    }
}

std::vector<TimerQueue::Entry> TimerQueue::getExpired(Timestamp now){
    // set 按到期时刻有序:从 begin() 连续取所有 <= now 的即可
    std::vector<Entry> expired;
    while(!timers_.empty() && timers_.begin()->first <= now){
        expired.push_back(*timers_.begin());
        timers_.erase(timers_.begin());
    }
    return expired;
}

void TimerQueue::resetTimerfd(Timestamp when){
    itimerspec its{};
    const int64_t ns =
    std::chrono::duration_cast<std::chrono::nanoseconds>(when.time_since_epoch()).count();
    its.it_value.tv_sec = static_cast<time_t>(ns/1000000000);
    its.it_value.tv_nsec = static_cast<long>(ns%1000000000);
    // it_interval 全 0 = 单次模式;repeat 由 handleRead 手动插回 set 实现
    ::timerfd_settime(timerfd_,TFD_TIMER_ABSTIME,&its,nullptr);
}
