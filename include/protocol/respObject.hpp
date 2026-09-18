#pragma once
#include <string>
#include <vector>

enum class RespType{
    SIMPLE_STRING,
    ERROR,
    INTEGER,
    BULK_STRING,
    ARRAY,
    NULL_BULK,
    NONE,
};

struct RespObject{
    RespType type = RespType::NONE;

    std::string str;
    long long integer = 0;
    std::vector<RespObject> elements;
};
