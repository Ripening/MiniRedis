#include "Buffer.h"
#include <errno.h>
#include <sys/uio.h>
#include <unistd.h>
Buffer::Buffer(size_t initAlSize)
    : buff_(kCheapPrepend + initAlSize)
    , readerIndex_(kCheapPrepend)
    , writerIndex_(kCheapPrepend)
{
}

ssize_t Buffer::readFd(int fd,int* saveError){
    //定义一个缓冲区
    char extraBuff[65535] = {0};
    iovec vec[2];
    vec[0].iov_base = writeBegin();
    vec[0].iov_len = writeAbleBytes();
    vec[1].iov_base = extraBuff;
    vec[1].iov_len = sizeof(extraBuff);

    int veccnt = (vec[0].iov_len<vec[1].iov_len)?2:1;
    const ssize_t n = ::readv(fd,vec,veccnt);
    if(n<0){
        //log
        *saveError = errno;
    }
    else if(n<vec[0].iov_len){
        writerIndex_+=n;
    }
    else{
        writerIndex_ = buff_.size();
        append(extraBuff, n - vec[0].iov_len);
    }
    return n;
}

ssize_t Buffer::writeFd(int fd,int* saveReeno){
    const ssize_t n = ::write(fd,peek(),readAbleBytes());
    if(n<0){
        *saveReeno = errno;
    }
    return n;
}

