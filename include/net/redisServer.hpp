#pragma once

#include "muduo/include/TcpServer.h"
#include "muduo/include/InetAddress.h"
#include "muduo/include/EventLoop.h"
#include "muduo/include/Callbacks.h"
#include "muduo/include/Buffer.h"
#include "command/commandDispatcher.hpp"
#include <string>
class RedisServer
{
public:
    RedisServer(int port,
            const std::string& name,
            TcpServer::Option option = TcpServer::Option::kNoReusePort);
    ~RedisServer() = default;

    EventLoop* getLoop() const{
        return redisServer_.getLoop();
    }

    void start();
private:
    
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn,Buffer* buf);

    void initialize();
private:
    EventLoop           mainLoop_;
    InetAddress         listenAddr_;
    TcpServer           redisServer_;

    CommandDispatcher   dispatcher_;
};

