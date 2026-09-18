#pragma once

#include <functional>
#include <thread>
#include <memory>
#include <unistd.h>
#include <string>
#include <atomic>
#include "noncopyable.h"
class Thread:public noncopyable
{
public:
    using ThreadFunc = std::function<void()>;
    explicit Thread(ThreadFunc func,const std::string& name = std::string());
    ~Thread();

    void start();
    void join();
   
    bool started() { return started_; }
    pid_t tid() const { return tid_; }
    const std::string &name() const { return name_; }

    static int numCreated() { return numCreated_; }
private:
    void setDefaultName();

    bool started_;
    bool joined_;
    ThreadFunc threadFunc_; //线程的回调函数
    std::shared_ptr<std::thread> thread_; //线程
    pid_t tid_; //线程号
    std::string name_; //当前线程的名称
    static inline std::atomic_int numCreated_{0};//一共创建的线程数
};

