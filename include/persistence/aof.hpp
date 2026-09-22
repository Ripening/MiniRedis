#pragma once
#include <string>
#include <chrono>
#include <future>
#include <vector>
#include <functional>
#include <sys/stat.h>
enum class AofFsyncPolicy{
    Always,
    EverySec,
    No,
};

class AOF
{
public:
    AOF(bool enabled = false,
        std::string path = "appendonly.aof",
        AofFsyncPolicy fsyncPolicy = AofFsyncPolicy::Always);
    ~AOF() = default;
    
    bool enabled() const{ return enabled_;}
    void setEnabled(bool enabled){ enabled_ = enabled;}
    const std::string& path() const{ return path_;}
    AofFsyncPolicy fsyncPolicy() const{ return fsyncPolicy_;}
    void setAofFsyncPolicy(AofFsyncPolicy policy){fsyncPolicy_ = policy;}
    // 追加一条命令到 AOF（RESP array 编码）。
    bool appendCommand(const std::vector<std::string>& argv, std::string& err);
    // 用一组恢复命令重写 AOF。先写临时文件，再原子替换目标文件。
    bool rewriteCommands(const std::vector<std::vector<std::string>>& commands, std::string& err);
    // 启动后台 AOF rewrite；真正文件替换在 pollBackgroundRewrite 中收尾。
    bool startBackgroundRewrite(const std::vector<std::vector<std::string>>& commands, std::string& err);
    // 事件循环周期调用：若后台 rewrite 已完成，则合并 rewrite buffer 并原子切换文件。
    bool pollBackgroundRewrite(std::string& err);
    bool backgroundRewriteInProgress() const { return backgroundRewriteInProgress_;}
    const std::string& lastBackgroundRewriteStatus() const{ return lastBackgroundRewriteStatus_;}
    // appendfsync everysec 使用：事件循环周期调用，必要时刷盘。
    bool flushIfNeeded(std::string& err, bool force = false);

    // 从 AOF 读取并顺序回放。apply 返回 false 时表示回放失败。
    bool replay(const std::function<bool(const std::vector<std::string>&, std::string&)>& apply,
                std::string& err) const;

    // AOF 文件当前大小；取不到(如文件还不存在)时返回 0 并填 err。
    size_t fileSize(std::string& err);
private:
    struct BackgroundRewriteResult{
        bool ok = false;
        std::string tempPath;
        std::string err;
    };
    bool fsyncPath(std::string& err);
    bool appendPayloadToFile(const std::string& path, const std::string& payload, bool append, std::string& err) const;

private:
    bool enabled_;  //开启aof
    std::string path_;  //aof写路径
    AofFsyncPolicy fsyncPolicy_;    //写策略
    bool dirty_;    //用来判断aof文件有新的数据写入，但未刷盘
    std::chrono::steady_clock::time_point   lastFsync_; //最后刷盘时间
    std::string lastBackgroundRewriteStatus_;   //最后后台重写状态

    bool backgroundRewriteInProgress_;  //正在进行后台重写
    std::future<BackgroundRewriteResult> backgroundRewriteFuture_;
    std::string backgroundRewriteBuffer_;   
};

