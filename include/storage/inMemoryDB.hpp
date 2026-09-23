#pragma once
#include "object/redisObject.hpp"
#include "core/dict.hpp"
#include "core/sds.hpp"
#include <string>
#include <vector>
#include <memory>
#include <chrono>
enum class DBStatus{
    OK = 0,
    NotFound,
    WrongType,
};
struct DBHashFieldEntry{
    std::string key;
    std::string value;
};
struct DBZSetEntry{
    std::string member;
    double score;
};

struct DBSnapshotEntry{
    redisObject::RedisObjectType type;  //数据类型
    std::string key;    //key值
    int64_t expiredAtMs;    //绝对过期时刻(ms)；-1 表示无过期

    std::string stringValue;    //作为字符串类型的数据值
    std::vector<DBHashFieldEntry> hashEntries; //作为哈希类型的数据值
    std::vector<DBZSetEntry> zsetEntries;   //作为zset类型的数据值
};

class InMemoryDB
{
public:
    InMemoryDB() = default;
    ~InMemoryDB() = default;

    void set(const std::string& key,const std::string& value);
    // GET key；返回 false 表示 key 不存在（或已过期）。
    bool get(const std::string& key, std::string& value);
    // GET key 的带类型版本；可区分不存在和类型错误。
    DBStatus getStringValue(const std::string& key, std::string& value);
    // DEL key；返回 1 表示删除成功，0 表示 key 不存在。
    bool del(const std::string& key);
    // EXISTS key；过期 key 视为不存在。
    bool exists(const std::string& key);
    // TYPE key；返回 false 表示 key 不存在(含已过期)。
    bool type(const std::string& key, redisObject::RedisObjectType& out);
    // INCR key；返回 false 时 err 带错误信息。
    bool incr(const std::string& key, long long& newValue, std::string& err);
    // INCRBY/DECR 的通用实现；delta 可正可负。
    bool incrBy(const std::string& key, long long delta, long long& newValue, std::string& err);
    
    
    // HSET key field value [field value ...]；返回本次新增 field 的数量。
    DBStatus hset(const std::string& key,
                  const std::vector<std::pair<std::string, std::string>>& fieldValues,
                  int& addedCount);
    // HGET key field；NotFound 表示 key 或 field 不存在。
    DBStatus hget(const std::string& key, const std::string& field, std::string& value);
    // HDEL key field [field ...]；返回删除的 field 数量。
    DBStatus hdel(const std::string& key, const std::vector<std::string>& fields, int& removedCount);
    // HEXISTS key field；NotFound 表示 key 或 field 不存在。
    DBStatus hexists(const std::string& key, const std::string& field, bool& exists);
    // HLEN key；NotFound 表示 key 不存在。
    DBStatus hlen(const std::string& key, size_t& len);
    // HGETALL/HKEYS/HVALS 的底层导出接口；NotFound 表示 key 不存在。
    DBStatus hgetall(const std::string& key, std::vector<DBHashFieldEntry>& entries);
    
    // ZADD key score member [score member ...]；仅新增成员计数,改分不算。
    DBStatus zadd(const std::string& key,
                const std::vector<std::pair<double, std::string>>& memberScores,
                int& addedCount);
    // ZRANGE/ZREVRANGE key start stop；start/stop 可负可越界,内部归一化。
    // NotFound 表示 key 不存在；reverse=true 即 ZREVRANGE。
    DBStatus zrangeByRank(const std::string& key, long long start, long long stop,
                        bool reverse, std::vector<DBZSetEntry>& entries);
    // ZRANGEBYSCORE key min max；范围由 handler 解析好后传入(兼容 ZREVRANGEBYSCORE)。
    DBStatus zrangeByScore(const std::string& key, const ScoreRange& range,
                         bool reverse, std::vector<DBZSetEntry>& entries);
    // ZRANK/ZREVRANK key member；rank 为 0-based。NotFound 表示 key 或 member 不存在。
    DBStatus zrank(const std::string& key, const std::string& member,
                 bool reverse, size_t& rank);

    // ZINCRBY key increment member；key 不存在时按 0 起算并新建。
    DBStatus zincrBy(const std::string& key, const std::string& member,
                   double delta, double& newScore);
    
    // ZPOPMIN/ZPOPMAX key [count]；最多弹 count 个,弹空后删除 key。
    DBStatus zpopMin(const std::string& key, size_t count, std::vector<DBZSetEntry>& popped);
    DBStatus zpopMax(const std::string& key, size_t count, std::vector<DBZSetEntry>& popped);
    
    // ZCARD key；NotFound 表示 key 不存在。
    DBStatus zcard(const std::string& key, size_t& len);
    // ZSCORE key member；NotFound 表示 key 或 member 不存在。
    DBStatus zscore(const std::string& key, const std::string& member, double& score);

    // EXPIRE key seconds；返回 1 表示设置成功，0 表示 key 不存在。
    int expire(const std::string& key, long long ttlSeconds);
    // TTL key；-2 不存在，-1 存在但无过期时间，>=0 剩余秒数。
    long long ttl(const std::string& key);
    // PTTL key；-2 不存在，-1 存在但无过期时间，>=0 剩余毫秒数。
    long long pttl(const std::string& key);
    // PERSIST key；返回 1 表示移除过期时间成功，0 表示无变化。
    int persist(const std::string& key);

    // 主动过期扫描；最多检查 sampleCount 个 TTL 条目，返回本轮删除数量。
    size_t activeExpireCycle(size_t sampleCount);

    // 导出当前有效数据(String/Hash)；已过期键跳过，expiredAtMs 为 -1 表示无过期时间。
    std::vector<DBSnapshotEntry> snapshot();


    // 获取当前单调时钟毫秒时间戳。
    static int64_t nowMs(){
        using namespace std::chrono;
        return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
    }
private:
    redisObject* getObject(const std::string& key);
    //惰性检查：检查key时效，过期立马删除
    bool expireIfNeed(const std::string& key);
    // 设置 key 的绝对过期时间（毫秒）；会覆盖旧值。
    void setExpireAtMs(const std::string& key, int64_t expireAtMs);
    // 删除 key 的过期时间；存在则释放内存并返回 true。
    bool eraseExpire(const std::string& key);
    // 获取 key 的绝对过期时间（毫秒）；不存在返回 false。
    bool getExpireAtMs(const std::string& key, int64_t& expireAtMs);
    
private:
    DICT<std::unique_ptr<redisObject>>  kv_;
    DICT<int64_t>                      expires_;
};



