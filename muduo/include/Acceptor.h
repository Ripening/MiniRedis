#pragma once

#include <functional>
#include <noncopyable.h>
#include "Channel.h"
#include "Socket.h"
class InetAddress;
class EventLoop;

class Acceptor:public noncopyable
{
public:
    using newConnectCallBack = std::function<void(int fd,const InetAddress&)>;
    Acceptor(EventLoop* loop,const InetAddress& listenAddress,bool resuport);
    ~Acceptor();
    void setnewConnectCallBack(const newConnectCallBack& cb){newConnectCallBack_=cb;}
    bool isListening()const{return listening;}
    void listen();
private:
    void handleRead();
    EventLoop* loop_;
    Socket acceptSocket;
    Channel acceptChannel;
    bool listening;
    newConnectCallBack newConnectCallBack_;
};

