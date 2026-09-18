#pragma once

#include "Poller.h"
#include <sys/epoll.h>
#include <vector>
class EPollPoller : public Poller
{
public:
    EPollPoller(EventLoop* loop);
    ~EPollPoller() override;
    void poll(int timeoutMs,ChannelList* activeChannels) override;
    void updateChannel(Channel* channel) override;
    void removeChannel(Channel* channel) override;

private:
    static const int initEventListSize = 16;
    void fillActiveChannels(int size,ChannelList* activeChannels);
    void update(int operation,Channel* channel);
    using EventList = std::vector<epoll_event>;
    EventList events_;
    int epollfd_;
};
