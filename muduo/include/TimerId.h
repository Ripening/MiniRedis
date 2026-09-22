#pragma once

#include <cstdint>

class Timer;

// 取消定时器用的值语义句柄。带序号是为了防 ABA:
// Timer 被 delete 后地址可能被新 Timer 复用,旧句柄靠序号就能识别出来。
class TimerId
{
public:
    TimerId():timer_(nullptr),sequence_(0){}
    TimerId(Timer* timer,int64_t seq):timer_(timer),sequence_(seq){}

    friend class TimerQueue;
private:
    Timer* timer_;
    int64_t sequence_;
};
