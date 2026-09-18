#include "core/sds.hpp"
#include <limits>
#include <stdlib.h>
#include <stdexcept>
//得到flag
static inline uint8_t sds_flags(const char* buf){
    return *(buf-1);
}
//填写头部信息
static void sds_set_header(void* sh, uint8_t type, size_t len, size_t alloc){
    switch (type)
    {
        case SDS::TYPE_8:{
            auto h = static_cast<SdsHdr8*>(sh);
            h->len = static_cast<uint8_t>(len);
            h->alloc = static_cast<uint8_t>(alloc);
            h->flag = type;
            break;
        }
        case SDS::TYPE_16:{
            auto h = static_cast<SdsHdr16*>(sh);
            h->len = static_cast<uint16_t>(len);
            h->alloc = static_cast<uint16_t>(alloc);
            h->flag = type;
            break;
        }
        case SDS::TYPE_32:{
            auto h = static_cast<SdsHdr32*>(sh);
            h->len = static_cast<uint32_t>(len);
            h->alloc = static_cast<uint32_t>(alloc);
            h->flag = type;
            break;
        }
        case SDS::TYPE_64:{
            auto h = static_cast<SdsHdr64*>(sh);
            h->len = static_cast<uint64_t>(len);
            h->alloc = static_cast<uint64_t>(alloc);
            h->flag = type;
            break;
        }
    }
}
//选择头部类型
uint8_t SDS::select_type(size_t len){
    if(len<=UINT8_MAX) return TYPE_8;
    else if(len<=UINT16_MAX) return TYPE_16;
    else if(len<=UINT32_MAX) return TYPE_32;
    else return TYPE_64;
}
//查找头部大小
size_t SDS::hdr_size(uint8_t type){
    switch (type)
    {
        case SDS::TYPE_8: return sizeof(SdsHdr8);
        case SDS::TYPE_16: return sizeof(SdsHdr16);
        case SDS::TYPE_32: return sizeof(SdsHdr32);
        case SDS::TYPE_64: return sizeof(SdsHdr64);
    }
    return 0;
}
SDS::SDS():buf_(nullptr){
    init("",0);
}
SDS::SDS(const char* buf){
    init(buf,strlen(buf));
}
SDS::SDS(std::string_view buf){
    init(buf.data(),buf.length());
}
void SDS::init(const char* str, size_t len){
    //选择type --> 计算头部大小
    uint8_t type = select_type(len);
    size_t hdrSize = hdr_size(type);
    //申请内存 （头部 + len + '\0'）
    void* sh = ::malloc(hdrSize+len+1);
    if(!sh) throw std::bad_alloc();
    //赋值buf指针
    buf_ = static_cast<char*>(sh) + hdrSize;
    //如果len>0 说明有数据，拷贝
    if(len>0){
        ::memcpy(buf_,str,len);
    }
    buf_[len] = '\0';
    sds_set_header(sh,type,len,len);
}
void SDS::destroy(){
    if(buf_ == nullptr) return;
    uint8_t flags = sds_flags(buf_);
    size_t type = flags & TYPE_MASK;

    void* sh = buf_ - hdr_size(type);
    ::free(sh);
    buf_ = nullptr;
}
SDS::~SDS(){
    destroy();
}
SDS::SDS(SDS&& other) noexcept{
    buf_ = other.buf_;
    other.buf_ = nullptr;
}
SDS& SDS::operator = (SDS&& other) noexcept{
    if(this!=&other){
        destroy();
        buf_ = other.buf_;
        other.buf_ = nullptr;
    }
    return *this;
}

size_t SDS::len() const{
    uint8_t flags = sds_flags(buf_);
    uint8_t type = flags & TYPE_MASK;
    void* sh = buf_ - hdr_size(type);
    switch (type)
    {
    case SDS::TYPE_8: return static_cast<SdsHdr8*>(sh)->len;
    case SDS::TYPE_16: return static_cast<SdsHdr16*>(sh)->len;
    case SDS::TYPE_32: return static_cast<SdsHdr32*>(sh)->len;
    case SDS::TYPE_64: return static_cast<SdsHdr64*>(sh)->len;
    }
    return 0;
}
size_t SDS::capacity() const{
    uint8_t flags = sds_flags(buf_);
    uint8_t type = flags & TYPE_MASK;
    void* sh = buf_ - hdr_size(type);
    switch (type)
    {
    case SDS::TYPE_8: return static_cast<SdsHdr8*>(sh)->alloc;
    case SDS::TYPE_16: return static_cast<SdsHdr16*>(sh)->alloc;
    case SDS::TYPE_32: return static_cast<SdsHdr32*>(sh)->alloc;
    case SDS::TYPE_64: return static_cast<SdsHdr64*>(sh)->alloc;
    }
    return 0;
}
size_t SDS::avail() const{
    return capacity() - len();
}

void SDS::setLen(size_t newLen){
    uint8_t flags = sds_flags(buf_);
    uint8_t type = flags & TYPE_MASK;
    void* sh = buf_ - hdr_size(type);
    switch (type)
    {
    case SDS::TYPE_8: static_cast<SdsHdr8*>(sh)->len = newLen; break;
    case SDS::TYPE_16: static_cast<SdsHdr16*>(sh)->len = newLen; break;
    case SDS::TYPE_32: static_cast<SdsHdr32*>(sh)->len = newLen; break;
    case SDS::TYPE_64: static_cast<SdsHdr64*>(sh)->len = newLen; break;
    }
}

const char* SDS::c_str() const{
    return buf_;
}

void SDS::clear(){
    setLen(0);
    buf_[0] = '\0';
}
void SDS::makeRoomFor(size_t addlen){
    size_t len = this->len();
    size_t alloc = capacity();
    if(alloc-len>=addlen) return;
    if(addlen> std::numeric_limits<size_t>::max()-len) throw std::length_error("length error");

    size_t newLen = len+addlen;
    size_t newAlloc;
    if(newLen<MAX_PREALLOC) newAlloc = newLen*2;
    else newAlloc = newLen + MAX_PREALLOC;

    uint8_t newType = select_type(newAlloc);
    size_t newHdrSize = hdr_size(newType);

    uint8_t oldType = (sds_flags(buf_)&TYPE_MASK);
    size_t olddrSize = hdr_size(oldType);
    void* oldSh = buf_ - olddrSize;
    void* newSh;
    if(newType==oldType){
        newSh = ::realloc(oldSh,newHdrSize+newAlloc+1);
        if(!newSh) throw std::bad_alloc();
    }
    else{
        newSh = ::malloc(newHdrSize+newAlloc+1);
        if(!newSh) throw std::bad_alloc();
        char* newbuf = static_cast<char*>(newSh) + newHdrSize;
        memcpy(newbuf, buf_, len + 1);
        free(oldSh);
    }
    buf_ = static_cast<char*>(newSh) + newHdrSize;
    sds_set_header(newSh,newType,len,newAlloc);
}
void SDS::append(const char* buf,size_t addLen){
    size_t curLen = this->len();
    makeRoomFor(addLen);
    ::memcpy(buf_+curLen,buf,addLen);
    setLen(curLen+addLen);
    buf_[curLen + addLen] = '\0';
}
void SDS::append(std::string_view str){
    append(str.data(),str.length());
}




