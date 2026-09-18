#pragma once
#include <functional>
#include <string>
#include <memory>
#include <atomic>
#include <unordered_map>

#include "EventLoop.h"
#include "Acceptor.h"
#include "InetAddress.h"
#include "noncopyable.h"
#include "EventLoopThreadPool.h"
#include "Callbacks.h"
#include "TcpConnection.h"
#include "Buffer.h"

class TcpServer
{
public:
    using ThreadInitCallback = std::function<void(EventLoop *)>;
    enum Option{
        kNoReusePort,
        kReusePort,
    };
    TcpServer(EventLoop* loop,
            const std::string& name,
            const InetAddress& listenAddr,
            Option option=kNoReusePort);
    ~TcpServer();
    
    void setConnectCallBack(const ConnectCallBack& cb){connectionCallback_=cb;}
    void setMessageCallBack(const MessageCallBack& cb){messageCallback_=cb;}
    void setWriteCompleteCallBack(const WriteCompleteCallBack& cb){writeCompleteCallback_=cb;}
    void setThreadInitCallback(const ThreadInitCallback& cb){threadInitCallback_ = cb;}

    void setNumThreads(int numThread);
    void start();

    EventLoop* getLoop() const{return loop_;}
private:
    void newConnection(int fd,const InetAddress& peerAddr);
    void removeConnection(const TcpConnectionPtr& conn);
    void removeConnectInLoop(const TcpConnectionPtr& conn);
    using ConnectionMap = std::unordered_map<std::string,TcpConnectionPtr>;
    EventLoop* loop_; //用户定义的base loop
    const std::string name_; //名称
    const std::string ipPort_; //IP地址+端口号
    std::unique_ptr<Acceptor> acceptor_;
    std::shared_ptr<EventLoopThreadPool> threadPoll_;

    ConnectCallBack connectionCallback_;       //有新连接时的回调
    MessageCallBack messageCallback_;             // 有读写事件发生时的回调
    WriteCompleteCallBack writeCompleteCallback_; // 消息发送完成后的回调
    ThreadInitCallback threadInitCallback_; // loop线程初始化的回调

    int numThreads_;
    std::atomic_int started_;
    int nextConnId_;
    ConnectionMap connections_;
};

