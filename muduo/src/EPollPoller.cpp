#include "EPollPoller.h"
#include "unistd.h"
#include "Channel.h"
#include <errno.h>
#include <stdio.h>
#include <cstring>
const int kNew = -1;
const int kAdded = 1;
const int kDeleted = 2;

EPollPoller::EPollPoller(EventLoop* loop)
    :Poller(loop),
    epollfd_(::epoll_create1(EPOLL_CLOEXEC)),
    events_(initEventListSize)
{
    if(epollfd_<0){
        //错误 记录日志
    }
}

EPollPoller::~EPollPoller(){
    ::close(epollfd_);
}

void EPollPoller::poll(int timeoutMs,ChannelList* activeChannels){
    int numEvents= epoll_wait(epollfd_,events_.data(),static_cast<int>(events_.size()),timeoutMs);
    int errnum = errno;
    if(numEvents>0){
        fillActiveChannels(numEvents,activeChannels);
        if(numEvents == events_.size()){
            events_.resize(events_.size()*2);
        }
    }
    else if(numEvents==0){
        //日志
    }
    else{
        //出现错误
        if(errnum!=EINTR){
            errno = errnum;
            //日志
        }
    }
    return;
}
void EPollPoller::fillActiveChannels(int size,ChannelList* activeChannels){
    for(int i=0;i<size;i++){
        Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
        channel->set_revent(events_[i].events);
        activeChannels->push_back(channel);
    }
}
void EPollPoller::updateChannel(Channel* channel){
    const int channel_index = channel->index();
    if(channel_index == kNew || channel_index == kDeleted){
        if(channel_index == kNew){
            int fd = channel->fd();
            channels_[fd] = channel;
        }
        else{
            //channel_index == kdelete
        }
        update(EPOLL_CTL_ADD,channel);
        channel->set_index(kAdded);
    }
    else{
        //channel的状态已经是added,看看是修改还是删除
        if(channel->isNoneEvent()){
            //对任何事件都不感兴趣
            update(EPOLL_CTL_DEL,channel);
            channel->set_index(kDeleted);
        }else{
            //只是修改
            update(EPOLL_CTL_MOD,channel);
        }
    }
}

void EPollPoller::update(int operation,Channel* channel){
    int fd = channel->fd();
    epoll_event event;
    ::memset(&event,0,sizeof(event));
    event.data.ptr = channel;
    event.events = channel->event();
    if(epoll_ctl(epollfd_,operation,fd,&event)<0){
        if(operation == EPOLL_CTL_DEL){
            //log
        }
        else{
            //log
        }
    }
}

void EPollPoller::removeChannel(Channel* channel){
    int fd = channel->fd();
    //从map中删除channel
    channels_.erase(fd);
    int index = channel->index();
    if(index==kAdded){
        update(EPOLL_CTL_DEL,channel);
    }
    channel->set_index(kNew);
}
