#pragma once

#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include <cstring>
class Buffer
{
public:
    static const size_t kCheapPrepend = 8;
    static const size_t kInitAlSize = 1024;
    explicit Buffer(size_t initAlSize = kInitAlSize);
    ~Buffer() = default;
    //返回buff状态游标
    size_t readAbleBytes()const{return writerIndex_ - readerIndex_;}
    size_t writeAbleBytes()const{return buff_.size()-writerIndex_;}
    size_t prependAbleBytes()const{return readerIndex_;}
    
    void retrieve(size_t len){
        if(len<readAbleBytes()){
            readerIndex_+=len;
        }else{
            readerIndex_ = kCheapPrepend;
            writerIndex_ = kCheapPrepend;
        }
    }
    void retrieveAll(){
        retrieve(readAbleBytes());
    }
    void retrieveUntil(const char* end){
        assert(end>=peek());
        assert(end<=writeBegin());
        retrieve(end-peek());
    }
    const char* peek()const{return begin()+readerIndex_;}
    std::string retrieveAllAsString(){
        return retrieveAsString(readAbleBytes());
    }
    std::string retrieveAsString(size_t len){
        std::string result(peek(),len);
        retrieve(len);
        return result;
    }
    void ensureWriteAbleBytes(size_t len){
        if(writeAbleBytes()<len){
            makeSpace(len);
        }
    }
    void append(const char* data,size_t len){
        ensureWriteAbleBytes(len);
        std::copy(data,data+len,writeBegin());
        writerIndex_+=len;
    }
    void append(const std::string& data){
        append(data.data(),data.size());
    }
    void append(const char* data){
        append(data,strlen(data));
    }
    char* writeBegin(){return begin()+writerIndex_;}
    const char* writeBegin() const{return begin()+writerIndex_;}

    ssize_t readFd(int fd,int* saveError);
    ssize_t writeFd(int fd,int* saveError);

    const char* findCRLF() const{
        const char* crlf = std::search(peek(),writeBegin(),kCRLF,kCRLF+2);
        return crlf==writeBegin()?nullptr:crlf;
    }
    const char* findCRLF(const char* start) const{
        assert(start>=peek());
        assert(start<=writeBegin());
        const char* crlf = std::search(start,writeBegin(),kCRLF,kCRLF+2);
        return crlf == writeBegin()? nullptr:crlf;
    }

private:
    char* begin(){return buff_.data();}
    const char* begin()const{return buff_.data();}

    void makeSpace(size_t len){
        if(writeAbleBytes()+prependAbleBytes()-kCheapPrepend<len){
            buff_.resize(writerIndex_+len);
        }
        else{
            size_t readable = readAbleBytes();
            std::copy(peek(),peek()+readable,begin()+kCheapPrepend);
            readerIndex_ = kCheapPrepend;
            writerIndex_ = readerIndex_ + readable;
        }
    }
private:
    std::vector<char> buff_;
    size_t readerIndex_;
    size_t writerIndex_;

    static constexpr char kCRLF[] = "\r\n";
};