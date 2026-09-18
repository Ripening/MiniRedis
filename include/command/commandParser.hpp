#pragma once
#include "protocol/respObject.hpp"
#include <string>
#include <vector>

class CommandParser
{
public:
    static bool toArgv(const RespObject& obj,std::vector<std::string>& argv,std::string& err){
        err.clear();
        argv.clear();
        if(obj.type!=RespType::ARRAY){
            err = "protocol error: expected array command";
            return false;
        }

        for(const auto& element:obj.elements){
            if(element.type!=RespType::BULK_STRING&&element.type!=RespType::SIMPLE_STRING){
                err = "protocol error: command element must be string";
                argv.clear();
                return false;
            }
            argv.push_back(element.str);
        }

        if(argv.empty()){
            err = "protocol error: empty command";
            return false;
        }
        return true;
    }
};

