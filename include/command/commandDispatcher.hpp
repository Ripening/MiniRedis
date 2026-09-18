#pragma once
#include "storage/inMemoryDB.hpp"
#include <string>
#include <vector>

class CommandDispatcher
{
public:
    CommandDispatcher() = default;
    ~CommandDispatcher() = default;

    std::string dispatch(const std::vector<std::string>& argv);
private:
    InMemoryDB db_;
};
