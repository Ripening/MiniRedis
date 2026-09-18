#pragma once
#include "protocol/respObject.hpp"
#include <string>

class RespParser
{
public:
    RespParser():pos_(0){}
    //喂数据
    void feed(const char* data, size_t len);
    //尝试解析一个数据包
    bool parse(RespObject& out);
    // 返回缓冲区中尚未消费的字节数，便于上层区分“等待更多数据”和“已完整消费完”。
    size_t pendingBytes() const;
private:
    bool parseInternal(RespObject&);
    
    bool parseSimpleString(RespObject&);
    bool parseError(RespObject&);
    bool parseInteger(RespObject&);
    bool parseBulkString(RespObject&);
    bool parseArray(RespObject&);

    bool readLine(std::string& line);
private:
    std::string buffer_;
    size_t pos_;
};
