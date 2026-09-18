#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>
#include <cstring>
class InetAddress
{
public:
    explicit InetAddress(uint16_t port = 0,std::string ip = "127.0.0.1"){
        ::memset(&addr_,0,sizeof(addr_));
        addr_.sin_family = AF_INET;
        addr_.sin_port = htons(port);
        addr_.sin_addr.s_addr = inet_addr(ip.c_str());
    }
    explicit InetAddress(const sockaddr_in& addr):addr_(addr){}
    ~InetAddress() = default;

    std::string toIp()const;
    std::string toIpPort()const;
    uint16_t toPort()const;

    const sockaddr_in* getInetAddress()const{return &addr_;}
    void setInetAddress(const sockaddr_in& addr){addr_ = addr;}
private:
    sockaddr_in addr_;
};