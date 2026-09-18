#include "Channel.h"
#include "EventLoop.h"
Channel::Channel(EventLoop* loop,int fd)
    :loop_(loop),
    fd_(fd),
    event_(0),
    revent_(0),
    index_(-1),
    tied_(false)
{
}
Channel::~Channel(){

}
void Channel::remove(){
    //调用eventloop的成员函数
    loop_->removeChannel(this);
}

void Channel::tied(const std::shared_ptr<void>& it){
    tie_ = it;
    tied_ = true;
}

void Channel::handleEvent(){
    if(tied_){
        auto lock = tie_.lock();
        if(lock){
            handleEventWithGard();
        }
    }else{
        handleEventWithGard();
    }
}

void Channel::handleEventWithGard(){
    if(revent_ & EPOLLHUP && !(revent_& kReadEvent)){
        if(closeCallBack_){
            closeCallBack_();
        }
    }
    if(revent_ & kReadEvent){
        if(readCallBack_){
            readCallBack_();
        }
    }
    if(revent_ & EPOLLERR){
        if(errCallBack_){
            errCallBack_();
        }
    }
    if(revent_ & kWriteEvent){
        if(writeCallBack_){
            writeCallBack_();
        }
    }
}

void Channel::update(){
    //调用eventloop的更新函数
    loop_->updateChannel(this);
}
