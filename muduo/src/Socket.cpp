#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>

#include "Socket.h"
#include "InetAddress.h"

Socket::~Socket(){
    ::close(sockfd_);
}

void Socket::bindAddress(const InetAddress& address){
    if(::bind(sockfd_,(sockaddr*)address.getInetAddress(),sizeof(sockaddr_in))!=0){
        // 绑定失败必须终止:不报错继续 listen 会被内核隐式绑定到随机端口,服务"活着"却不可达
        // (原版 muduo 的 bindOrDie 同样是致命语义:打印 errno 后 abort)
        perror("Socket::bindAddress");
        ::exit(EXIT_FAILURE);
    }
}
void Socket::listen(){
    if(::listen(sockfd_,1024)!=0){
        perror("Socket::listen");
        ::exit(EXIT_FAILURE);
    }
}
int Socket::accept(InetAddress* peerAddress){
    sockaddr_in sin;
    socklen_t len = sizeof(sin);
    ::memset(&sin,0,sizeof(sin));
    int connfd = ::accept4(sockfd_,(sockaddr*)&sin,&len,SOCK_NONBLOCK|SOCK_CLOEXEC);
    if(connfd>=0){
        peerAddress->setInetAddress(sin);
    }
    return connfd;
}
void Socket::shutdown(){
    if(::shutdown(sockfd_,SHUT_WR)<0){
        //log
    }
}
void Socket::setTcpNoDelay(bool on)
{
    // TCP_NODELAY 用于禁用 Nagle 算法。
    // Nagle 算法用于减少网络上传输的小数据包数量。
    // 将 TCP_NODELAY 设置为 1 可以禁用该算法，允许小数据包立即发送。
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval));
}

void Socket::setReuseAddr(bool on)
{
    // SO_REUSEADDR 允许一个套接字强制绑定到一个已被其他套接字使用的端口。
    // 这对于需要重启并绑定到相同端口的服务器应用程序非常有用。
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
}

void Socket::setReusePort(bool on)
{
    // SO_REUSEPORT 允许同一主机上的多个套接字绑定到相同的端口号。
    // 这对于在多个线程或进程之间负载均衡传入连接非常有用。
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_REUSEPORT, &optval, sizeof(optval));
}

void Socket::setKeepAlive(bool on)
{
    // SO_KEEPALIVE 启用在已连接的套接字上定期传输消息。
    // 如果另一端没有响应，则认为连接已断开并关闭。
    // 这对于检测网络中失效的对等方非常有用。
    int optval = on ? 1 : 0;
    ::setsockopt(sockfd_, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval));
}
