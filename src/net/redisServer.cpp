#include "net/redisServer.hpp"
#include "protocol/respParser.hpp"
#include "protocol/respObject.hpp"
#include "protocol/respEncoder.hpp"
#include "command/commandParser.hpp"
#include <string>
#include <vector>

RedisServer::RedisServer(int port,
            const std::string& name,
            TcpServer::Option option):
            listenAddr_(port),
            redisServer_(&mainLoop_,name,listenAddr_,option){
    initialize();
}

void RedisServer::start(){
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
    redisServer_.setConnectCallBack([this](const TcpConnectionPtr& conn){
        onConnection(conn);
    });
    redisServer_.setMessageCallBack([this](const TcpConnectionPtr& conn,Buffer* buf){
        onMessage(conn,buf);
    });
}