#include "net/redisServer.hpp"

int main(int argc, char const *argv[])
{
    RedisServer server(6379,"mini-redis");
    server.start();
    return 0;
}
