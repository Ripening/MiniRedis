#pragma once

#include <string>
#include <string_view>
#include <vector>

class RespEncoder
{
public:
    static std::string simpleString(std::string_view str){
        std::string out;
        out.reserve(str.size() + 3);
        out += '+';
        out += str;
        out += "\r\n";
        return out;
    }

    static std::string error(std::string_view str){
        std::string out;
        out.reserve(str.size() + 3);
        out += '-';
        out += str;
        out += "\r\n";
        return out;
    }

    static std::string integer(long long value){
        std::string out;
        out += ':';
        out += std::to_string(value);
        out += "\r\n";
        return out;
    }

    static std::string bulkString(std::string_view s){
        std::string out;
        out.reserve(s.size() + 16);
        out += '$';
        out += std::to_string(s.size());
        out += "\r\n";
        out += s;
        out += "\r\n";
        return out;
    }

    static std::string nullBulk(){
        return "$-1\r\n";
    }

    static std::string array(const std::vector<std::string>& elements){
        std::string out;
        out += '*';
        out += std::to_string(elements.size());
        out += "\r\n";
        for(const auto& s : elements){
            out += bulkString(s);
        }
        return out;
    }
};
