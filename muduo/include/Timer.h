#pragma once

#include "noncopyable.h"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <utility>

// 全项目统一时间类型。steady_clock 在 Linux 上就是 CLOCK_MONOTONIC，
// 可以直接取 time_since_epoch() 换算给 timerfd。
using Timestamp = std::chrono::steady_clock::time_point;

using TimerCallback = std::function<void()>;

inline Timestamp nowTimestamp(){
    return std::chrono::steady_clock::now();
}

// 加若干秒(支持小数)。steady_clock 的 duration 是整数纳秒,必须显式转换
inline Timestamp addSeconds(Timestamp tp,double seconds){
    return tp + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    std::chrono::duration<double>(seconds));
}

// 单个定时器:回调 + 下次到期时刻(绝对) + 间隔(0 表示一次性) + 序号
class Timer : public noncopyable
{
public:
    Timer(TimerCallback cb,Timestamp when,double intervalSeconds)
        :callback_(std::move(cb)),
        expiration_(when),
        interval_(intervalSeconds),
        repeat_(intervalSeconds > 0.0),
        sequence_(nextSequence())
    {
    }

    void run() const { callback_(); }

    Timestamp expiration() const { return expiration_; }
    bool repeat() const { return repeat_; }
    int64_t sequence() const { return sequence_; }

    // 重复定时器的下次时刻 = now + interval(不是 expiration + interval:
    // 回调执行慢了不能追着连续补触发)
    void restart(Timestamp now){
        expiration_ = addSeconds(now,interval_);
    }

private:
    const TimerCallback callback_;
    Timestamp expiration_;
    const double interval_;
    const bool repeat_;
    const int64_t sequence_;

    static int64_t nextSequence(){
        static std::atomic<int64_t> counter{0};
        return counter++;
    }
};
