#include "object/redisObject.hpp"

redisObject::redisObject(RedisObjectType type,RedisObjectEncoding encoding,void* ptr):
    type_(type),encoding_(encoding),ptr_(ptr){

}
redisObject::~redisObject(){
    freeRedisObject(type_);
}
void redisObject::freeRedisObject(RedisObjectType type){
    if(ptr_ == nullptr) return;
    switch (type)
    {
    case RedisObjectType::STRING:
        delete static_cast<SDS*>(ptr_);
        break;
    case RedisObjectType::HASH:
        delete static_cast<DICT<SDS>*>(ptr_);
        break;
    case RedisObjectType::ZSET:
        delete static_cast<ZSet*>(ptr_);
        break;
    }
    ptr_ = nullptr;
}
