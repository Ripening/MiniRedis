#include "TcpServer.h"
#include "TcpConnection.h"
#include <functional>
#include <cstring>
#include <cstdio>

static EventLoop* checkLoopNotNull(EventLoop* loop){
    if(loop==nullptr){
        //log
    }
    return loop;
}

TcpServer::TcpServer(EventLoop* loop,
            const std::string& name,
            const InetAddress& listenAddr,
            Option option)
    :loop_(checkLoopNotNull(loop)),
    name_(name),
    ipPort_(listenAddr.toIpPort()),
    acceptor_(new Acceptor(loop_,listenAddr,option == kReusePort)),
    threadPoll_(new EventLoopThreadPool(loop_,name_)),
    started_(0),
    nextConnId_(1),
    numThreads_(0)
{
    acceptor_->setnewConnectCallBack([this](int fd, const InetAddress& peerAddr){
        this->newConnection(fd,peerAddr);
    });
}
TcpServer::~TcpServer(){
    for(auto& items:connections_){
       TcpConnectionPtr conn(items.second);
       items.second.reset();
       conn->getLoop()->runInLoop([conn](){
            conn->connectDestroyed();
       });
    }
}
void TcpServer::setNumThreads(int numThread){
    numThreads_ = numThread;
    threadPoll_->setThreadNum(numThreads_);
}

void TcpServer::start(){
    if(started_.fetch_add(1)==0){
        threadPoll_->start(threadInitCallback_);
        loop_->runInLoop([this](){
            acceptor_->listen();
        });
    }
}

void TcpServer::newConnection(int fd,const InetAddress& peerAddr){
    //轮询获得下一个subloop指针
    EventLoop* ioLoop = threadPoll_->getNextLoop();
    //拼接connection的名字
    char buf[64]={0};
    snprintf(buf,sizeof(buf),"-%s#%d",ipPort_.c_str(),nextConnId_);
    nextConnId_++;
    std::string connName = name_+buf;
    //得到当前套接字实际绑定的地址信息结构体
    sockaddr_in local;
    ::memset(&local,0,sizeof(local));
    socklen_t len = sizeof(local);
    if(::getsockname(fd,(sockaddr*)&local,&len)<0){
        //log error
    }
    InetAddress localAddr(local);
    //定义一个connection并存放到map中
    TcpConnectionPtr conn(new TcpConnection(ioLoop,connName,fd,localAddr,peerAddr));
    connections_[connName] = conn;
    //给定义的connection设置各种回调函数
    conn->setConnectCallBack(connectionCallback_);
    conn->setWriteCompleteCallBack(writeCompleteCallback_);
    conn->setMessageCallBack(messageCallback_);

    conn->setCloseCallBack([this](const TcpConnectionPtr& conn){
        this->removeConnection(conn);
    });

    ioLoop->runInLoop([conn](){conn->connectEstablished();});
}

void TcpServer::removeConnection(const TcpConnectionPtr& conn){
    loop_->runInLoop([this,conn](){
        this->removeConnectInLoop(conn);
    });
}

void TcpServer::removeConnectInLoop(const TcpConnectionPtr& conn){
    //log
    //从map删除此连接
    connections_.erase(conn->getName());
    //得到此连接的所属的eventloop
    EventLoop* ioLoop = conn->getLoop();
    //在所属的loop中执行连接的销毁
    ioLoop->queueInLoop([conn]{conn->connectDestroyed();});
}


