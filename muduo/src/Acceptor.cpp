#include "Acceptor.h"
#include "InetAddress.h"
#include "EventLoop.h"
#include <sys/socket.h>     // socket, AF_INET, SOCK_STREAM etc.
#include <netinet/in.h>

int createNoneBlock(){
    int connfd = ::socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,IPPROTO_TCP);
    if(connfd<0){
        //log
    }
    return connfd;
}
Acceptor::Acceptor(EventLoop* loop,const InetAddress& listenAddress,bool resuport)
    :loop_(loop)
    ,acceptSocket(createNoneBlock())
    ,acceptChannel(loop,acceptSocket.fd())
    ,listening(false)
{
    acceptSocket.setReuseAddr(true);
    acceptSocket.setReusePort(resuport);
    acceptSocket.bindAddress(listenAddress);

    acceptChannel.setReadEvent([this](){
        this->handleRead();
    });
}
Acceptor::~Acceptor(){
    acceptChannel.disableAll();
    acceptChannel.remove();
}
void Acceptor::listen(){
    listening=true;
    acceptSocket.listen();
    acceptChannel.enableReading();
}
void Acceptor::handleRead(){
    InetAddress peerAddress;
    int connfd = acceptSocket.accept(&peerAddress);
    if(connfd>=0){
        if(newConnectCallBack_){
            newConnectCallBack_(connfd,peerAddress);
        }
        else{
            ::close(connfd);
        }
    }
    else{
        //log
    }
}
