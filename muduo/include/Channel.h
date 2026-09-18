#pragma once

#include "noncopyable.h"
#include <memory>
#include <functional>
#include <sys/epoll.h>

class EventLoop;

class Channel : public noncopyable
{
public:
    using EventCallBack = std::function<void()>;
    using ReadEventCallBack = std::function<void()>;
    Channel(EventLoop* loop,int fd);
    ~Channel();

    int fd()const{return fd_;}
    int event()const{return event_;}
    void set_revent(int revent){revent_=revent;}
    //设置fd感兴趣的事件类型
    void enableReading(){event_ |= kReadEvent; update();}
    void disableReading(){event_ &= ~kReadEvent;update();}
    void enableWriting(){event_ |= kWriteEvent;update();}
    void disableWriting(){event_ &= ~kWriteEvent;update();}
    void disableAll(){event_ = kNoneEvent;update();}
    //检查fd的事件类型
    bool isReading(){return event_ & kReadEvent;}
    bool isWriting(){return event_ & kWriteEvent;}
    bool isNoneEvent(){return event_ == kNoneEvent;}
    //fd本身的状态
    int index(){return index_;}
    void set_index(int index){index_=index;}
    //设置回调函数
    void setReadEvent(ReadEventCallBack readCallBack){readCallBack_ = std::move(readCallBack);}
    void setWriteEvent(EventCallBack writeCallBack){writeCallBack_ = std::move(writeCallBack);}
    void setErrEvent(EventCallBack errCallBack){errCallBack_ = std::move(errCallBack);}
    void setCloseEvent(EventCallBack closeCallBack){closeCallBack_ = std::move(closeCallBack);}
    //所属的eventloop
    EventLoop* ownerLoop(){return loop_;}
    //从loop中移除channel
    void remove();
    //让channel知道上层的存活状态
    void tied(const std::shared_ptr<void>& it);
    //根据fd关心的事件类型
    void handleEvent();
private:
    void update();
    void handleEventWithGard();
private:
    int fd_; //文件描述符
    int event_; //fd感兴趣的事件类型
    int revent_; //实际发生的事件类型
    int index_; //channel的状态
    EventLoop* loop_;
    std::weak_ptr<void> tie_;
    bool tied_;

    inline static const int kNoneEvent = 0;
    inline static const int kReadEvent = EPOLLIN | EPOLLPRI;
    inline static const int kWriteEvent = EPOLLOUT;

    ReadEventCallBack readCallBack_;
    EventCallBack writeCallBack_;
    EventCallBack errCallBack_; 
    EventCallBack closeCallBack_;

};
