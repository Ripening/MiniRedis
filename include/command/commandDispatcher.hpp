#pragma once
#include "storage/inMemoryDB.hpp"
#include "persistence/aof.hpp"
#include <string>
#include <vector>

class CommandDispatcher
{
public:
    CommandDispatcher() = default;
    ~CommandDispatcher() = default;

    std::string dispatch(const std::vector<std::string>& argv);

    bool loadAof();
    bool rewriteAof(std::string& err);
    bool backgroundRewriteAof(std::string& err);

    const std::string& lastError() const {return lastError_;}

    // 启动时配置用:默认关闭，由服务器决定何时打开。
    AOF& aof() { return aof_; }

    // 事件循环周期性调用：执行主动过期扫描和 AOF everysec 刷盘。
    void cron();

    static long long lastRewriteSize(){return lastRewriteSize_;}
private:
    std::string dispatchInternal(const std::vector<std::string>& argv, bool replayingAof);
    std::vector<std::vector<std::string>> snapshotCommands();
private:
    InMemoryDB  db_;
    AOF         aof_;

    std::string lastError_;
    static inline long long lastRewriteSize_ = 65536;
};
