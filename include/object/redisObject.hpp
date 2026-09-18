#pragma once
#include <string>
#include <memory>
#include <string_view>
#include "core/sds.hpp"
#include "core/dict.hpp"
class redisObject
{
public:
    enum class RedisObjectType{
        STRING = 0,
        HASH   = 1,
    };
    enum class RedisObjectEncoding{
        RAW = 0,
        HASHTABLE = 1,
    };
public:
    static inline std::unique_ptr<redisObject> createStringObject(std::string_view str){
        return std::unique_ptr<redisObject>(new redisObject(RedisObjectType::STRING,RedisObjectEncoding::RAW,new SDS(str)));
    }
    static inline std::unique_ptr<redisObject> createHashObject(){
        return std::unique_ptr<redisObject>(new redisObject(RedisObjectType::HASH,RedisObjectEncoding::HASHTABLE,new DICT<SDS>()));
    }
    RedisObjectType type() const { return type_; }

    static inline const SDS* getStringObjectValue(const redisObject* obj){
        if(obj->ptr_ == nullptr || obj->type_ != RedisObjectType::STRING) return nullptr;
        
        return static_cast<SDS*>(obj->ptr_);
    }
    static inline SDS* getStringObjectValue(redisObject* obj){
        if(obj->ptr_ == nullptr || obj->type_ != RedisObjectType::STRING) return nullptr;
        
        return static_cast<SDS*>(obj->ptr_);
    }
    static inline const DICT<SDS>* getHashObjectValue(const redisObject* obj){
        if(obj->ptr_ == nullptr || obj->type_ != RedisObjectType::HASH) return nullptr;

        return static_cast<DICT<SDS>*>(obj->ptr_);
    }
    static inline DICT<SDS>* getHashObjectValue(redisObject* obj){
        if(obj->ptr_ == nullptr || obj->type_ != RedisObjectType::HASH) return nullptr;

        return static_cast<DICT<SDS>*>(obj->ptr_);
    }
    ~redisObject();

    redisObject(const redisObject&) = delete;
    redisObject& operator=(const redisObject&) = delete;
    redisObject(redisObject&&) noexcept = delete;
    redisObject& operator=(redisObject&&) noexcept = delete;
private:
    redisObject() = delete;
    redisObject(RedisObjectType type,RedisObjectEncoding encoding,void* ptr);
    void freeRedisObject(RedisObjectType type);
private:
    RedisObjectType     type_;
    RedisObjectEncoding encoding_;
    void*               ptr_;
};



