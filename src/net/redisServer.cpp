#include "net/redisServer.hpp"
#include "protocol/respParser.hpp"
#include "protocol/respObject.hpp"
#include "protocol/respEncoder.hpp"
#include "command/commandParser.hpp"
#include <string>
#include <vector>
#include <iostream>
#include <csignal>
#include <cstdlib>

namespace{
    // 信号处理函数里只能碰 sig_atomic_t,收尾动作全部交给事件循环去做
    volatile sig_atomic_t g_stop = 0;
    void onStopSignal(int){ g_stop = 1; }
}

RedisServer::RedisServer(int port,
            const std::string& name,
            TcpServer::Option option):
            listenAddr_(port),
            redisServer_(&mainLoop_,name,listenAddr_,option){
    initialize();
}

void RedisServer::start(){
    if(!dispatcher_.loadAof()){
        std::cerr << "load AOF failed: " << dispatcher_.lastError() << std::endl;
        std::exit(1);
    }
    mainLoop_.runEvery(1.0, [this]{ dispatcher_.cron(); });

    struct sigaction sa{};
    sa.sa_handler = onStopSignal;
    sigaction(SIGINT,&sa,nullptr);
    sigaction(SIGTERM,&sa,nullptr);
    // 信号本身只置了标志,这里轮询到再刷一次 AOF 然后退出循环
    mainLoop_.runEvery(0.2,[this]{
        if(!g_stop) return;
        std::string err;
        if(!dispatcher_.aof().flushIfNeeded(err,true)){
            std::cerr << "flush AOF failed: " << err << std::endl;
        }
        std::cout << "stop signal received, aof flushed" << std::endl;
        mainLoop_.quit();
    });

    redisServer_.start();
    mainLoop_.loop();
    std::cout << "mini-redis server stopped" << std::endl;
}

void RedisServer::onConnection(const TcpConnectionPtr& conn){
    if(conn->connected()){
        conn->setContext(RespParser{});
        conn->setTcpNoDelay(true);
    }
}

void RedisServer::onMessage(const TcpConnectionPtr& conn,Buffer* buf){
    auto* parse = std::any_cast<RespParser>(conn->getMutableContext());
    parse->feed(buf->peek(),buf->readAbleBytes());
    buf->retrieveAll();

    std::string reply;
    RespObject out;
    try
    {   
        std::string err;
        while(parse->parse(out)){
            std::vector<std::string> argv;
            if(!CommandParser::toArgv(out,argv,err)){
                conn->send(RespEncoder::error(err));
                conn->shutdown();
                return;
            }
            reply += dispatcher_.dispatch(argv);
        }
    }
    catch(const std::exception& e)
    {
        conn->send(RespEncoder::error(std::string("ERR Protocol error: ") + e.what()));
        conn->shutdown();
        return;
    }
    if(!reply.empty()) conn->send(reply);
}

void RedisServer::initialize(){
    // AOF 默认关闭，由服务器在启动配置阶段打开
    dispatcher_.aof().setEnabled(true);
    dispatcher_.aof().setAofFsyncPolicy(AofFsyncPolicy::EverySec);
    redisServer_.setConnectCallBack([this](const TcpConnectionPtr& conn){
        onConnection(conn);
    });
    redisServer_.setMessageCallBack([this](const TcpConnectionPtr& conn,Buffer* buf){
        onMessage(conn,buf);
    });
}