#include "storage/inMemoryDB.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <utility>

static bool addWouldOverflow(long long base, long long delta){
    if(base>0&&delta>std::numeric_limits<long long>::max()-base) return true;
    if(base<0&&delta<std::numeric_limits<long long>::min()-base) return true;
    return false;
}

void InMemoryDB::set(const std::string& key,const std::string& value){
    expireIfNeed(key);
    
    auto newRedisObj = redisObject::createStringObject(value);
    kv_.set(SDS(key),std::move(newRedisObj));

    eraseExpire(key);
}
bool InMemoryDB::get(const std::string& key, std::string& value){
    return getStringValue(key,value) == DBStatus::OK;
}
DBStatus InMemoryDB::getStringValue(const std::string& key, std::string& value){
    redisObject* obj = getObject(key);           // 内部含 expireIfNeed + 查表
    if (obj == nullptr) return DBStatus::NotFound;
    SDS* s = redisObject::getStringObjectValue(obj);
    if (s == nullptr) return DBStatus::WrongType;
    value.assign(s->c_str(), s->len());
    return DBStatus::OK;
}
bool InMemoryDB::del(const std::string& key){
    expireIfNeed(key);
    expires_.erase(SDS(key));
    return kv_.erase(SDS(key));
}
bool InMemoryDB::exists(const std::string& key){
    expireIfNeed(key);
    return kv_.get(SDS(key)) != nullptr;
}
bool InMemoryDB::incr(const std::string& key, long long& newValue, std::string& err){
    return incrBy(key,1,newValue,err);
}
bool InMemoryDB::incrBy(const std::string& key, 
            long long delta, 
            long long& newValue, 
            std::string& err){
    err.clear();
    long long value = 0;
    redisObject* obj = getObject(key);
    //拿到 RedisObject 对象
    if(obj!=nullptr){
        //拿到内部存储的数据
        const SDS* str = redisObject::getStringObjectValue(obj);
        if(str == nullptr){
            err = "WRONGTYPE Operation against a key holding the wrong kind of value";
            return false;
        }
        //把 string 数据转换为 long long 
        try
        {
            size_t pos = 0;
            value = std::stoll(std::string(str->c_str()),&pos,10);
            if(pos!=str->len()){
                err = "ERR value is not an integer or out of range";
                return false;
            }
        }
        catch(const std::exception& e)
        {
            err = "ERR value is not an integer or out of range";
            return false;
        }
    }

    //检查 value + delta 是否溢出
    if(addWouldOverflow(value,delta)){
        err = "ERR increment or decrement would overflow";
        return false;
    }

    newValue = value + delta;
    auto newobj = redisObject::createStringObject(std::to_string(newValue));

    return kv_.set(SDS(key),std::move(newobj));
}

DBStatus InMemoryDB::hset(const std::string& key,
                  const std::vector<std::pair<std::string, std::string>>& fieldValues,
                  int& addedCount){
    addedCount = 0;
    redisObject* obj = getObject(key);
    if(obj == nullptr){
        auto hash = redisObject::createHashObject();
        kv_.set(SDS(key),std::move(hash));
    }else if(redisObject::getHashObjectValue(obj) == nullptr){
        return DBStatus::WrongType;
    }

    obj = getObject(key);
    DICT<SDS>* hash = redisObject::getHashObjectValue(obj);

    for(const auto& [filed,value]:fieldValues){
        SDS sfield(filed);
        auto result = hash->get(sfield);
        hash->set(SDS(filed),SDS(value));
        if(result == nullptr) addedCount++;
    }
    return DBStatus::OK;
}

DBStatus InMemoryDB::hget(const std::string& key, const std::string& field, std::string& value){
    redisObject* obj = getObject(key);
    if(obj==nullptr){
        return DBStatus::NotFound;
    }
    auto hash = redisObject::getHashObjectValue(obj);
    if(hash == nullptr){
        return DBStatus::WrongType;
    }
    auto v = hash->get(SDS(field));
    if(v == nullptr){
        return  DBStatus::NotFound;
    }
    value.assign(v->c_str(), v->len());
    return DBStatus::OK;
}
DBStatus InMemoryDB::hdel(const std::string& key, const std::vector<std::string>& fields, int& removedCount){
    removedCount = 0;
    redisObject* obj = getObject(key);
    if (obj == nullptr) {
        return DBStatus::NotFound;
    }
    auto hash = redisObject::getHashObjectValue(obj);
    if(hash == nullptr){
        return DBStatus::WrongType;
    }
    for (const std::string& field : fields) {
        SDS sField(field);
        auto raw = hash->get(sField);
        if (raw == nullptr) {
            continue;
        }
        if (hash->erase(sField)) {
            ++removedCount;
        }
    }

    if (hash->size() == 0) {
        const SDS sKey(key);
        kv_.erase(sKey);
        eraseExpire(key);
    }
    return DBStatus::OK;
}

DBStatus InMemoryDB::hexists(const std::string& key, const std::string& field, bool& exists){
    exists = false;
    redisObject* obj = getObject(key);
    if(obj == nullptr) return DBStatus::NotFound;
    auto hash = redisObject::getHashObjectValue(obj);
    if(hash == nullptr) return DBStatus::WrongType;

    if(hash->get(SDS(field)) != nullptr){
        exists = true;
        return DBStatus::OK;
    }
    return DBStatus::NotFound;
}

DBStatus InMemoryDB::hlen(const std::string& key, size_t& len){
    len = 0;
    redisObject* obj = getObject(key);
    if(obj == nullptr) return DBStatus::NotFound;
    auto hash = redisObject::getHashObjectValue(obj);
    if(hash == nullptr) return DBStatus::WrongType;
    len = hash->size();
    if(len == 0){
        const SDS sKey(key);
        kv_.erase(sKey);
        eraseExpire(key);
    }
    return DBStatus::OK;
}

DBStatus InMemoryDB::hgetall(const std::string& key, std::vector<DBHashFieldEntry>& entries){
    entries.clear();

    redisObject* obj = getObject(key);
    if (obj == nullptr) {
        return DBStatus::NotFound;
    }

    auto hash = redisObject::getHashObjectValue(obj);
    if(hash == nullptr) return DBStatus::WrongType;

    hash->forEach([&entries](const SDS& field, const SDS& rawValue) {
        entries.push_back(DBHashFieldEntry {std::string(field.c_str(), field.len()),
                                            std::string(rawValue.c_str(), rawValue.len())});
    });

    std::sort(entries.begin(), entries.end(), [](const DBHashFieldEntry& lhs, const DBHashFieldEntry& rhs) {
        return lhs.key < rhs.key;
    });

    return DBStatus::OK;
}


int InMemoryDB::expire(const std::string& key, long long ttlSeconds){
    expireIfNeed(key);

    if (kv_.get(SDS(key)) == nullptr) {
        (void)eraseExpire(key);
        return 0;
    }

    if (ttlSeconds <= 0) {
        (void)del(key);
        return 1;
    }

    const int64_t now = nowMs();
    int64_t expireAt = std::numeric_limits<int64_t>::max();
    const int64_t maxDeltaSec = (std::numeric_limits<int64_t>::max() - now) / 1000;
    if (ttlSeconds <= maxDeltaSec) {
        expireAt = now + ttlSeconds * 1000;
    }
    setExpireAtMs(key, expireAt);
    return 1;
}

long long InMemoryDB::ttl(const std::string& key){
    const long long remainMx = pttl(key);
    if(remainMx <0) return remainMx;
    return remainMx / 1000;
}

long long InMemoryDB::pttl(const std::string& key){
    expireIfNeed(key);

    //在 kv表中查询，没有直接返回-2
    if(kv_.get(SDS(key)) == nullptr){
        (void)eraseExpire(key);
        return -2;
    }
    //kv表中存在，查expire表
    int64_t expireAtMs = 0;
    if(!getExpireAtMs(key,expireAtMs)){
        return -1;
    }
    const int64_t remainMs = expireAtMs - nowMs();
    if (remainMs <= 0) {
        (void)del(key);
        return -2;
    }
    return remainMs;
}

int InMemoryDB::persist(const std::string& key){
    expireIfNeed(key);
    if(kv_.get(SDS(key))==nullptr){
        eraseExpire(key);
        return 0;
    }
    return eraseExpire(key)?1:0;
}

size_t InMemoryDB::activeExpireCycle(size_t sampleCount){
    if(sampleCount == 0||expires_.size()==0) return 0;

    size_t checked = 0;
    size_t removed = 0;
    const int64_t now = nowMs();
    std::vector<std::string> expiredKeys;
    expiredKeys.reserve(sampleCount);
    expires_.forEach([&](const SDS & key, const int64_t &value){
        if(checked>sampleCount) return;
        checked++;
        if(now>value){
            expiredKeys.emplace_back(key.c_str(), key.len());
        }
    });

    for(std::string key : expiredKeys){
        if(!eraseExpire(key)) continue;
        
        kv_.erase(SDS(key));
        removed ++;
    }

    return removed;
}

std::vector<DBSnapshotEntry> InMemoryDB::snapshot(){
    (void)activeExpireCycle(expires_.size());

    std::vector<DBSnapshotEntry> entries;
    entries.reserve(kv_.size());
    const int64_t now = nowMs();

    kv_.forEach([&](const SDS& key, const std::unique_ptr<redisObject>& value){
        const std::string keyStr(key.c_str(), key.len());

        int64_t expireAtMs = 0;
        const bool hasExpire = getExpireAtMs(keyStr, expireAtMs);
        if (hasExpire && expireAtMs <= now) {
            return;                     // 没被惰性清掉的过期键，不进快照
        }

        const redisObject* obj = value.get();
        DBSnapshotEntry entry {};
        entry.key = keyStr;
        entry.type = obj->type();
        entry.expiredAtMs = hasExpire ? expireAtMs : -1;

        if (const SDS* strValue = redisObject::getStringObjectValue(obj); strValue != nullptr) {
            entry.stringValue.assign(strValue->c_str(), strValue->len());
            entries.push_back(std::move(entry));
            return;
        }

        const DICT<SDS>* hash = redisObject::getHashObjectValue(obj);
        if (hash == nullptr) {
            return;                     // 未知类型，当前不可能
        }

        entry.hashEntries.reserve(hash->size());
        hash->forEach([&](const SDS& field, const SDS& fieldValue){
            entry.hashEntries.push_back(DBHashFieldEntry {
                std::string(field.c_str(), field.len()),
                std::string(fieldValue.c_str(), fieldValue.len())
            });
        });
        entries.push_back(std::move(entry));
    });

    return entries;
}

bool InMemoryDB::expireIfNeed(const std::string& key){
    int64_t expireAtMs = 0;
    //没有过期时间
    if(!getExpireAtMs(key,expireAtMs)) return false;
    //没到过期时间
    if(expireAtMs > nowMs()) return false;
    
    const SDS skey(key);
    kv_.erase(skey);
    eraseExpire(key);
    return true;
}
void InMemoryDB::setExpireAtMs(const std::string& key, int64_t expireAtMs){
    expires_.set(SDS(key),expireAtMs);
}
bool InMemoryDB::getExpireAtMs(const std::string& key, int64_t& expireAtMs){
    auto expireTime = expires_.get(SDS(key));
    if(expireTime == nullptr) return false;
    expireAtMs = *expireTime;
    return true;
}
bool InMemoryDB::eraseExpire(const std::string& key){
    return expires_.erase(SDS(key));
}

redisObject* InMemoryDB::getObject(const std::string& key){
    expireIfNeed(key);
    const std::unique_ptr<redisObject>* p = kv_.get(SDS(key));
    return p == nullptr ? nullptr : p->get();
}