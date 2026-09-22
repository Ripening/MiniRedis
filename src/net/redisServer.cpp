#include "net/redisServer.hpp"
#include "protocol/respParser.hpp"
#include "protocol/respObject.hpp"
#include "protocol/respEncoder.hpp"
#include "command/commandParser.hpp"
#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>
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
    redisServer_.start();
    mainLoop_.loop();
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