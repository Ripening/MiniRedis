#include "InetAddress.h"

std::string InetAddress::toIp()const{
    char ip[64] = {0};
    ::inet_ntop(AF_INET,&addr_.sin_addr,ip,sizeof(ip));
    return ip;
}

uint16_t InetAddress::toPort()const{
    return ntohs(addr_.sin_port);
}

std::string InetAddress::toIpPort()const{
    uint16_t port = toPort();
    char ipPort[64]={0};
    ::inet_ntop(AF_INET,&addr_.sin_addr,ipPort,sizeof(ipPort));
    size_t len = ::strlen(ipPort);
    snprintf(ipPort+len,sizeof(ipPort)-len,":%d",port);
    return ipPort;
}