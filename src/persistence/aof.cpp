#include "persistence/aof.hpp"
#include "protocol/respEncoder.hpp"
#include "protocol/respParser.hpp"
#include "protocol/respObject.hpp"
#include "command/commandParser.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <future>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <functional>

namespace{
    bool writeAll(int fd, const char* data, size_t len, std::string& err){
        err.clear();
        size_t written = 0;
        while (written < len)
        {
            const ssize_t count = ::write(fd, data+written, len-written);
            if(count < 0){
                if(errno == EINTR) continue;
                err = std::strerror(errno);
                return false;
            }
            if (count == 0) {
                err = "short write";
                return false;
            }
            written += static_cast<size_t>(count);
        } 
        return true;
    }
}

AOF::AOF(bool enabled,
        std::string path,
        AofFsyncPolicy fsyncPolicy):
        enabled_(enabled),
        path_(path),
        fsyncPolicy_(fsyncPolicy),
        dirty_(false),
        lastFsync_(std::chrono::steady_clock::now()),
        lastBackgroundRewriteStatus_("ok"),
        backgroundRewriteInProgress_(false),
        backgroundRewriteFuture_(),
        backgroundRewriteBuffer_(){}

bool AOF::appendCommand(const std::vector<std::string>& argv, std::string& err){
    err.clear();
    if(!enabled_) return true;

    const std::string payload = RespEncoder::array(argv);
    int fd = ::open(path_.c_str(),O_CREAT | O_WRONLY | O_APPEND, 0644);
    if(fd < 0){
        err = std::strerror(errno);
        return false;
    }
    bool ok = writeAll(fd,payload.data(),payload.size(),err);
    if(ok && fsyncPolicy_ == AofFsyncPolicy::Always && ::fsync(fd) != 0){
        err = std::strerror(errno);
        ok = false;
    }
    if(::close(fd) && ok){
        err = std::strerror(errno);
        ok = false;
    }
    if(ok && fsyncPolicy_ == AofFsyncPolicy::EverySec){
        dirty_ = true;
    }
    if(ok && backgroundRewriteInProgress_){
        backgroundRewriteBuffer_ += payload;
    }

    return ok;
}

bool AOF::rewriteCommands(const std::vector<std::vector<std::string>>& commands, std::string& err){
    err.clear();
    if(!enabled_) return true;
     
    if(backgroundRewriteInProgress_){
        err = "background AOF rewrite already in progress";
        return false;
    }
    std::string tempPath = path_ + ".tmp";
    int fd = ::open(tempPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC,0644);
    if(fd<0){
        err = std::strerror(errno);
        return false;
    }
    bool ok = true;
    for(const auto& argv:commands){
        const std::string payload = RespEncoder::array(argv);
        if(!writeAll(fd,payload.data(),payload.size(),err)){
            ok = false;
            continue;
        }
    }
    //落盘
    if(ok && ::fsync(fd) != 0){
        ok = false;
        err = std::strerror(errno);
        ::unlink(tempPath.c_str());
    }
    //关闭文件描述符
    if(::close(fd)!=0 && ok){
        ok = false;
        err = std::strerror(errno);
        ::unlink(tempPath.c_str());
    }
    if(!ok){
        ::unlink(tempPath.c_str());
        return false;
    }
    //替换文件 （原子性）
    if(::rename(tempPath.c_str(),path_.c_str())!=0){
        ok = false;
        err = std::strerror(errno);
        ::unlink(tempPath.c_str());
        return false;
    }

    dirty_ = false;
    lastFsync_ = std::chrono::steady_clock::now();
    return true;
}

bool AOF::startBackgroundRewrite(const std::vector<std::vector<std::string>>& commands, std::string& err){
    err.clear();
    if(!enabled_) return true;

    if(backgroundRewriteInProgress_){
        err = "background AOF rewrite already in progress";
        return false;
    }

    const std::string tempPath = path_ + ".tmp.bg";
    //开始后台重写
    backgroundRewriteInProgress_ = true;
    backgroundRewriteBuffer_.clear();
    // std::async 启动失败(线程资源耗尽)会抛异常,兜住:
    // 否则 in_progress_ 卡在 true,后续所有重写请求被永久拒绝
    try{
    backgroundRewriteFuture_ = std::async(std::launch::async,[commands,tempPath](){
        BackgroundRewriteResult result;
        //打开文件
        std::string err;
        result.tempPath = tempPath;
        const int fd = ::open(tempPath.c_str(), O_CREAT|O_WRONLY|O_TRUNC, 0644);
        if(fd < 0){
            result.err = std::strerror(errno);
            result.ok = false;
            return result;
        }

        bool ok = true;
        for(const auto& argv:commands){
            const std::string payload = RespEncoder::array(argv);
            if(!writeAll(fd,payload.data(),payload.size(),err)){
                ok = false;
                continue;
            }
        }

        if(ok && ::fsync(fd) != 0){
            ok = false;
            err = std::strerror(errno);
        }
        if(::close(fd) != 0 && ok){
            ok = false;
            err = std::strerror(errno);
        }
        if(!ok){
            ::unlink(tempPath.c_str());
            result.err = err;
            return result;
        }

        result.ok = true;
        return result;
    });
    }
    catch(const std::exception& e){
        backgroundRewriteInProgress_ = false;
        lastBackgroundRewriteStatus_ = "err";
        err = std::string("failed to start background rewrite: ") + e.what();
        return false;
    }

    lastBackgroundRewriteStatus_ = "in_progress";
    return true;
}

bool AOF::pollBackgroundRewrite(std::string& err){
    err.clear();
    if(!backgroundRewriteInProgress_) return true;

    if(!backgroundRewriteFuture_.valid()){
        backgroundRewriteInProgress_ = false;
        lastBackgroundRewriteStatus_ = "err";
        err = "background rewrite future is invalid";
        return false;
    }

    if(backgroundRewriteFuture_.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready){
        return true;
    }

    // 后台任务内部抛异常时 get() 会重抛;不兜住的话异常穿过 cron
    // 一路炸进 EventLoop 定时器回调,整个进程终止
    BackgroundRewriteResult result;
    try{
        result = backgroundRewriteFuture_.get();
    }
    catch(const std::exception& e){
        backgroundRewriteInProgress_ = false;
        backgroundRewriteBuffer_.clear();
        lastBackgroundRewriteStatus_ = "err";
        err = std::string("background rewrite failed: ") + e.what();
        return false;
    }
    backgroundRewriteInProgress_ = false;

    if(!result.ok){
        backgroundRewriteBuffer_.clear();
        lastBackgroundRewriteStatus_ = "err";
        err = result.err.empty()? "background rewrite failed":result.err;
        return false;
    }
    // 把 buf 中的数据追加到临时文件里面
    if(!backgroundRewriteBuffer_.empty()){
        if(!appendPayloadToFile(result.tempPath,backgroundRewriteBuffer_,true,err)){
            backgroundRewriteBuffer_.clear();
            lastBackgroundRewriteStatus_ = "err";
            (void)::unlink(result.tempPath.c_str());
            return false;
        }
    }
    backgroundRewriteBuffer_.clear();

    //替换
    if(::rename(result.tempPath.c_str(),path_.c_str()) != 0){
        lastBackgroundRewriteStatus_ = "err";
        err = std::strerror(errno);
        (void)::unlink(result.tempPath.c_str());
        return false;   
    }

    dirty_ = false;
    lastFsync_ = std::chrono::steady_clock::now();
    lastBackgroundRewriteStatus_ = "ok";

    return true;
}


bool AOF::appendPayloadToFile(const std::string& path, const std::string& payload, bool append, std::string& err) const{
    err.clear();
    int flag = (O_WRONLY | O_CREAT | (append?O_APPEND : O_TRUNC));
    const int fd = ::open(path.c_str(),flag);
    if(fd < 0){
        err = std::strerror(errno);
        return false;
    }
    bool ok = writeAll(fd, payload.data(), payload.size(), err);
    if (ok && ::fsync(fd) != 0) {
        err = std::strerror(errno);
        ok = false;
    }
    if (::close(fd) != 0 && ok) {
        err = std::strerror(errno);
        ok = false;
    }
    return ok;
}

bool AOF::flushIfNeeded(std::string& err, bool force){
    err.clear();
    if(!enabled_ || fsyncPolicy_ != AofFsyncPolicy::EverySec || !dirty_){
        return true;
    }

    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFsync_).count(); 
    if(!force && elapsedMs < 1000){
        return true;
    }
    if(!fsyncPath(err)){
        return false;
    }
    dirty_ = false;
    lastFsync_ = now;
    return true;
}

size_t AOF::fileSize(std::string& err){
    err.clear();
    struct stat fileInfo;
    if(::stat(path_.c_str(),&fileInfo) == -1){
        err = std::strerror(errno);
        return 0;
    }
    return static_cast<size_t>(fileInfo.st_size);
}

bool AOF::fsyncPath(std::string& err){
    err.clear();
    const int fd = ::open(path_.c_str(),O_RDONLY);
    if(fd < 0){
        err = std::strerror(errno);
        return false;
    }
    bool ok = true;
    if(::fsync(fd) != 0 && ok){
        err = std::strerror(errno);
        ok = false;
    }
    if(::close(fd) != 0 && ok){
        err = std::strerror(errno);
        ok = false;
    }

    return ok;
}

bool AOF::replay(const std::function<bool(const std::vector<std::string>&, std::string&)>& apply,
                std::string& err) const{
    err.clear();
    if(!enabled_) return true;
    std::ifstream in(path_,std::ios::binary);
    if(!in.good()) return true;

    std::string content((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
    RespParser parser;
    parser.feed(content.data(),content.size());

    while(true){
        RespObject obj;
        bool ok = true;
        try
        {
           ok = parser.parse(obj);
        }
        catch(const std::exception& e)
        {
            err = std::string("AOF parse failed: ") + e.what();
            return false;
        }
        if(!ok) break;
        std::vector<std::string> argv;
        std::string parseErr;
        if (!CommandParser::toArgv(obj, argv, parseErr)) {
            err = "AOF command parse failed: " + parseErr;
            return false;
        }

        std::string applyErr;
        if (!apply(argv, applyErr)) {
            err = "AOF replay command failed: " + applyErr;
            return false;
        }
    }

    return true;
}