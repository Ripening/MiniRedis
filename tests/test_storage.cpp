#include <gtest/gtest.h>
#include "storage/inMemoryDB.hpp"

#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

const DBSnapshotEntry* findEntry(const std::vector<DBSnapshotEntry>& entries, const std::string& key){
    for (const auto& e : entries) {
        if (e.key == key) return &e;
    }
    return nullptr;
}

std::string findField(const std::vector<DBHashFieldEntry>& fields, const std::string& name){
    for (const auto& f : fields) {
        if (f.key == name) return f.value;
    }
    return "<missing>";
}

} // namespace

// ---------- String 基础 ----------

TEST(InMemoryDBTest, SetGetDelExists) {
    InMemoryDB db;
    std::string out;

    EXPECT_FALSE(db.get("k", out));          // 不存在的键
    db.set("k", "hello");
    ASSERT_TRUE(db.get("k", out));
    EXPECT_EQ(out, "hello");
    EXPECT_TRUE(db.exists("k"));

    EXPECT_TRUE(db.del("k"));
    EXPECT_FALSE(db.exists("k"));
    EXPECT_FALSE(db.del("k"));               // 再删返回 0
}

TEST(InMemoryDBTest, SetOverwritesValue) {
    InMemoryDB db;
    std::string out;
    db.set("k", "v1");
    db.set("k", "v2");
    ASSERT_TRUE(db.get("k", out));
    EXPECT_EQ(out, "v2");
}

// 值里含 '\0' 必须原样存取(二进制安全),c_str() 截断的写法会在这里露馅
TEST(InMemoryDBTest, BinarySafeValues) {
    InMemoryDB db;
    const std::string bin("a\0b", 3);
    db.set("k", bin);

    std::string out;
    ASSERT_TRUE(db.get("k", out));
    EXPECT_EQ(out.size(), 3u);
    EXPECT_EQ(out, bin);
}

// ---------- INCR ----------

TEST(InMemoryDBTest, IncrOnMissingKeyStartsFromZero) {
    InMemoryDB db;
    long long v = -1;
    std::string err;

    EXPECT_TRUE(db.incr("n", v, err));
    EXPECT_EQ(v, 1);
    EXPECT_TRUE(db.incr("n", v, err));
    EXPECT_EQ(v, 2);

    EXPECT_TRUE(db.incrBy("n", -5, v, err));
    EXPECT_EQ(v, -3);
}

TEST(InMemoryDBTest, IncrRejectsNonIntegerAndOverflow) {
    InMemoryDB db;
    long long v = 0;
    std::string err;

    db.set("s", "abc");
    EXPECT_FALSE(db.incr("s", v, err));
    EXPECT_FALSE(err.empty());

    db.set("max", "9223372036854775807");
    EXPECT_FALSE(db.incr("max", v, err));    // 溢出必须被拦下

    int added = 0;
    db.hset("h", {{"f", "1"}}, added);
    EXPECT_FALSE(db.incr("h", v, err));      // WRONGTYPE
}

// ---------- TTL ----------

TEST(InMemoryDBTest, TtlSemantics) {
    InMemoryDB db;
    db.set("k", "v");

    EXPECT_EQ(db.ttl("k"), -1);              // 存在但无过期时间
    EXPECT_EQ(db.ttl("missing"), -2);        // 不存在
    EXPECT_EQ(db.expire("missing", 10), 0);  // 对不存在的键设置失败

    EXPECT_EQ(db.expire("k", 10), 1);
    const long long t = db.ttl("k");
    EXPECT_GE(t, 9);                         // 刚设置,剩余应接近 10 秒
    EXPECT_LE(t, 10);

    EXPECT_EQ(db.persist("k"), 1);           // 移除过期时间
    EXPECT_EQ(db.ttl("k"), -1);
    EXPECT_EQ(db.persist("k"), 0);           // 已无 TTL,移除无效

    EXPECT_EQ(db.expire("k", 0), 1);         // ttl <= 0 直接删键
    EXPECT_FALSE(db.exists("k"));
}

// SET 会清除已有 TTL(Redis 语义)
TEST(InMemoryDBTest, SetClearsTtl) {
    InMemoryDB db;
    db.set("k", "v1");
    db.expire("k", 100);
    EXPECT_GT(db.ttl("k"), 0);

    db.set("k", "v2");
    EXPECT_EQ(db.ttl("k"), -1);
}

// 回归:del 必须同时清掉 expires_ 里的记录,
// 否则之后用 INCR/HSET 重建的同名键会"继承"旧 TTL
TEST(InMemoryDBTest, DelDoesNotLeakTtlToRebuiltKey) {
    InMemoryDB db;
    db.set("k", "v");
    db.expire("k", 100);
    db.del("k");

    long long v = 0;
    std::string err;
    ASSERT_TRUE(db.incr("k", v, err));       // incr 不主动清 TTL
    EXPECT_EQ(db.ttl("k"), -1);              // 新键不应带旧 TTL
}

// 过期键的两条清理路径 + 快照跳过。只 sleep 一次:
// 三个键的 TTL 都是 1 秒,睡完后:lazy 走惰性删除,sweep 走主动扫描,snap 由快照流程处理
TEST(InMemoryDBTest, ExpiredKeysLazyActiveAndSnapshot) {
    InMemoryDB db;
    db.set("lazy", "v");
    db.set("sweep", "v");
    db.set("snap", "v");
    db.expire("lazy", 1);
    db.expire("sweep", 1);
    db.expire("snap", 1);

    std::this_thread::sleep_for(std::chrono::milliseconds(1200));

    // ① 惰性删除:访问即清理,对外表现为"键不存在"
    EXPECT_EQ(db.ttl("lazy"), -2);
    std::string out;
    EXPECT_FALSE(db.get("lazy", out));

    // ② 主动扫描:从没被访问过的过期键由 cycle 清掉
    EXPECT_GE(db.activeExpireCycle(64), 1u);
    EXPECT_FALSE(db.exists("sweep"));

    // ③ 快照:过期键不能出现在导出结果里
    const auto snap = db.snapshot();
    EXPECT_EQ(findEntry(snap, "snap"), nullptr);
    EXPECT_TRUE(snap.empty());
}

// ---------- Hash ----------

TEST(InMemoryDBTest, HashBasics) {
    InMemoryDB db;
    int added = 0;
    ASSERT_EQ(db.hset("h", {{"f1", "v1"}, {"f2", "v2"}}, added), DBStatus::OK);
    EXPECT_EQ(added, 2);

    std::string val;
    EXPECT_EQ(db.hget("h", "f1", val), DBStatus::OK);
    EXPECT_EQ(val, "v1");
    EXPECT_EQ(db.hget("h", "nope", val), DBStatus::NotFound);
    EXPECT_EQ(db.hget("missing", "f", val), DBStatus::NotFound);

    added = -1;
    ASSERT_EQ(db.hset("h", {{"f1", "v1b"}}, added), DBStatus::OK);
    EXPECT_EQ(added, 0);                     // 更新已有 field 不算新增
    ASSERT_EQ(db.hget("h", "f1", val), DBStatus::OK);
    EXPECT_EQ(val, "v1b");

    bool exists = false;
    EXPECT_EQ(db.hexists("h", "f1", exists), DBStatus::OK);
    EXPECT_TRUE(exists);
    EXPECT_EQ(db.hexists("h", "nope", exists), DBStatus::NotFound);
    EXPECT_FALSE(exists);

    size_t len = 0;
    ASSERT_EQ(db.hlen("h", len), DBStatus::OK);
    EXPECT_EQ(len, 2u);

    std::vector<DBHashFieldEntry> all;
    ASSERT_EQ(db.hgetall("h", all), DBStatus::OK);
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].key, "f1");             // hgetall 按 field 名排序
    EXPECT_EQ(all[1].key, "f2");
    EXPECT_EQ(all[1].value, "v2");
}

// HDEL 删空最后一个 field 后,键本身必须消失(Redis 语义)
TEST(InMemoryDBTest, HdelRemovesEmptyHashKey) {
    InMemoryDB db;
    int added = 0;
    db.hset("h", {{"f1", "v1"}, {"f2", "v2"}}, added);

    int removed = 0;
    ASSERT_EQ(db.hdel("h", {"f2", "nope"}, removed), DBStatus::OK);
    EXPECT_EQ(removed, 1);                   // 不存在的 field 不计入
    EXPECT_TRUE(db.exists("h"));

    ASSERT_EQ(db.hdel("h", {"f1"}, removed), DBStatus::OK);
    EXPECT_EQ(removed, 1);
    EXPECT_FALSE(db.exists("h"));
}

TEST(InMemoryDBTest, HashWrongType) {
    InMemoryDB db;
    db.set("s", "x");

    std::string val;
    EXPECT_EQ(db.hget("s", "f", val), DBStatus::WrongType);

    size_t len = 0;
    EXPECT_EQ(db.hlen("s", len), DBStatus::WrongType);

    bool exists = false;
    EXPECT_EQ(db.hexists("s", "f", exists), DBStatus::WrongType);
}

// ---------- snapshot ----------

TEST(InMemoryDBTest, SnapshotExportsStringAndHash) {
    InMemoryDB db;
    db.set("s1", "hello");
    db.set("s2", "world");
    db.expire("s2", 100);
    int added = 0;
    db.hset("h1", {{"f1", "v1"}, {"f2", "v2"}}, added);

    const auto snap = db.snapshot();
    ASSERT_EQ(snap.size(), 3u);

    const DBSnapshotEntry* s1 = findEntry(snap, "s1");
    ASSERT_NE(s1, nullptr);
    EXPECT_EQ(s1->type, redisObject::RedisObjectType::STRING);
    EXPECT_EQ(s1->stringValue, "hello");
    EXPECT_EQ(s1->expiredAtMs, -1);          // 无过期
    EXPECT_TRUE(s1->hashEntries.empty());

    const DBSnapshotEntry* s2 = findEntry(snap, "s2");
    ASSERT_NE(s2, nullptr);
    EXPECT_GT(s2->expiredAtMs, 0);           // 带 TTL,存的是绝对时刻

    const DBSnapshotEntry* h1 = findEntry(snap, "h1");
    ASSERT_NE(h1, nullptr);
    EXPECT_EQ(h1->type, redisObject::RedisObjectType::HASH);
    EXPECT_EQ(h1->expiredAtMs, -1);
    ASSERT_EQ(h1->hashEntries.size(), 2u);
    // 内层字段顺序未定义(哈希表遍历序),按名字查找
    EXPECT_EQ(findField(h1->hashEntries, "f1"), "v1");
    EXPECT_EQ(findField(h1->hashEntries, "f2"), "v2");
    EXPECT_TRUE(h1->stringValue.empty());
}
