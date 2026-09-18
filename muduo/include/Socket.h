#pragma once

#include "noncopyable.h"

class InetAddress;
class Socket:public noncopyable
{
public:
    explicit Socket(int fd):sockfd_(fd){}
    ~Socket();

    int fd()const{return sockfd_;}
    void bindAddress(const InetAddress& address);
    void listen();
    int accept(InetAddress* peerAddress);

    void shutdown();

    void setTcpNoDelay(bool on);
    void setReuseAddr(bool on);
    void setReusePort(bool on);
    void setKeepAlive(bool on);

private:
    const int sockfd_;
};


