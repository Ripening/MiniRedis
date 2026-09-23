#include "net/redisServer.hpp"
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

int main(int argc, char const *argv[])
{
    int port = 6379;
    std::string dir = ".";

    for(int i = 1; i<argc; i++){
        const std::string opt = argv[i];
        if(opt == "--port"&&i+1<argc){
            port = std::atoi(argv[++i]);
        }else if(opt == "--dir"&&i+1<argc){
            dir = argv[++i];
        }else{
            std::cerr << "usage: miniredis_server [--port 6379] [--dir .]" << std::endl;
            return 1;
        }
    }
    if(port<=0||port>65535){
        std::cerr << "invalid port: " << port << std::endl;
        return 1;
    }

    // AOF 写在进程工作目录里,和真 Redis 的 dir 是一个意思:先切过去再起服务
    if(chdir(dir.c_str())!=0){
        std::cerr << "can not chdir to " << dir << std::endl;
        return 1;
    }

    RedisServer server(port,"mini-redis");
    server.start();
    return 0;
}
