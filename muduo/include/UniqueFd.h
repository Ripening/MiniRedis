#pragma once

#include "noncopyable.h"
#include <unistd.h>

// 文件描述符的 RAII 包装:move-only,析构即关闭
class UniqueFd:public noncopyable
{
public:
    UniqueFd()=default;
    explicit UniqueFd(int fd):fd_(fd){}
    ~UniqueFd(){
        if(fd_>=0) ::close(fd_);
    }

    UniqueFd(UniqueFd&& other)noexcept:fd_(other.fd_){
        other.fd_=-1;
    }
    UniqueFd& operator=(UniqueFd&& other)noexcept{
        if(this!=&other){
            reset();
            fd_=other.fd_;
            other.fd_=-1;
        }
        return *this;
    }

    int get()const{return fd_;}
    void reset(){
        if(fd_>=0) ::close(fd_);
        fd_=-1;
    }

private:
    int fd_=-1;
};
