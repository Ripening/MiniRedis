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

std::vector<std::string> membersOf(const std::vector<DBZSetEntry>& entries){
    std::vector<std::string> out;
    for (const auto& e : entries) {
        out.push_back(e.member);
    }
    return out;
}

// 闭区间 [min, max]
ScoreRange scoreRange(double min, double max) { return ScoreRange{min, max}; }

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

// ---------- ZSet ----------

TEST(InMemoryDBTest, ZSetBasics) {
    InMemoryDB db;
    int added = -1;
    ASSERT_EQ(db.zadd("z", {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}}, added), DBStatus::OK);
    EXPECT_EQ(added, 3);

    added = -1;
    ASSERT_EQ(db.zadd("z", {{9.0, "a"}, {4.0, "d"}}, added), DBStatus::OK);
    EXPECT_EQ(added, 1);                     // a 只改分不算新增,d 才是新的

    double score = 0;
    ASSERT_EQ(db.zscore("z", "a", score), DBStatus::OK);
    EXPECT_DOUBLE_EQ(score, 9.0);
    EXPECT_EQ(db.zscore("z", "nope", score), DBStatus::NotFound);
    EXPECT_EQ(db.zscore("missing", "a", score), DBStatus::NotFound);

    size_t len = 0;
    ASSERT_EQ(db.zcard("z", len), DBStatus::OK);
    EXPECT_EQ(len, 4u);

    // 分数: b=2, c=3, d=4, a=9
    size_t rank = 99;
    ASSERT_EQ(db.zrank("z", "b", false, rank), DBStatus::OK);
    EXPECT_EQ(rank, 0u);                     // 0-based,最小的 b 排第一
    ASSERT_EQ(db.zrank("z", "b", true, rank), DBStatus::OK);
    EXPECT_EQ(rank, 3u);                     // 逆序时 b 垫底
    EXPECT_EQ(db.zrank("z", "nope", false, rank), DBStatus::NotFound);
    EXPECT_EQ(db.zrank("missing", "a", false, rank), DBStatus::NotFound);
}

TEST(InMemoryDBTest, ZRangeByRankForwardAndReverse) {
    InMemoryDB db;
    int added = 0;
    db.zadd("z", {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}, {4.0, "d"}}, added);

    std::vector<DBZSetEntry> out;
    ASSERT_EQ(db.zrangeByRank("z", 0, -1, false, out), DBStatus::OK);
    EXPECT_EQ(membersOf(out), (std::vector<std::string>{"a", "b", "c", "d"}));
    EXPECT_DOUBLE_EQ(out[3].score, 4.0);

    ASSERT_EQ(db.zrangeByRank("z", 1, 2, false, out), DBStatus::OK);
    EXPECT_EQ(membersOf(out), (std::vector<std::string>{"b", "c"}));   // 子区间

    ASSERT_EQ(db.zrangeByRank("z", 0, -1, true, out), DBStatus::OK);
    EXPECT_EQ(membersOf(out), (std::vector<std::string>{"d", "c", "b", "a"}));

    ASSERT_EQ(db.zrangeByRank("z", 0, 999, false, out), DBStatus::OK);  // stop 越界截到表尾
    EXPECT_EQ(out.size(), 4u);

    ASSERT_EQ(db.zrangeByRank("missing", 0, -1, false, out), DBStatus::NotFound);
}

// 区间方法必须自己清空出参,调用方复用 vector 时不能累积旧数据
TEST(InMemoryDBTest, ZRangeClearsOutputVector) {
    InMemoryDB db;
    int added = 0;
    db.zadd("z", {{1.0, "a"}}, added);

    std::vector<DBZSetEntry> out;
    ASSERT_EQ(db.zrangeByRank("z", 0, -1, false, out), DBStatus::OK);
    EXPECT_EQ(out.size(), 1u);

    ASSERT_EQ(db.zrangeByRank("z", 0, -1, false, out), DBStatus::OK);   // 复用同一个 vector
    EXPECT_EQ(out.size(), 1u);                                          // 不是 2

    ASSERT_EQ(db.zrangeByScore("z", scoreRange(0, 10), false, out), DBStatus::OK);
    EXPECT_EQ(out.size(), 1u);                                          // 也不能带进上一次的残留

    ASSERT_EQ(db.zrangeByScore("z", scoreRange(10, 20), false, out), DBStatus::OK);
    EXPECT_TRUE(out.empty());                                           // 无命中时要清干净
}

TEST(InMemoryDBTest, ZRangeByScoreBoundsAndReverse) {
    InMemoryDB db;
    int added = 0;
    db.zadd("z", {{1.0, "a"}, {2.0, "b"}, {3.0, "c"}}, added);

    std::vector<DBZSetEntry> out;
    ASSERT_EQ(db.zrangeByScore("z", scoreRange(2, 3), false, out), DBStatus::OK);
    EXPECT_EQ(membersOf(out), (std::vector<std::string>{"b", "c"}));

    ASSERT_EQ(db.zrangeByScore("z", scoreRange(1, 1), false, out), DBStatus::OK);
    EXPECT_EQ(membersOf(out), (std::vector<std::string>{"a"}));

    ASSERT_EQ(db.zrangeByScore("z", scoreRange(1.5, 2.5), false, out), DBStatus::OK);
    EXPECT_EQ(membersOf(out), (std::vector<std::string>{"b"}));         // 缝隙里只夹到一个

    ASSERT_EQ(db.zrangeByScore("z", scoreRange(1, 3), true, out), DBStatus::OK);
    EXPECT_EQ(membersOf(out), (std::vector<std::string>{"c", "b", "a"}));  // 逆序

    ASSERT_EQ(db.zrangeByScore("z", scoreRange(10, 20), false, out), DBStatus::OK);
    EXPECT_TRUE(out.empty());

    ASSERT_EQ(db.zrangeByScore("missing", scoreRange(1, 3), false, out), DBStatus::NotFound);
}

TEST(InMemoryDBTest, ZSetWrongType) {
    InMemoryDB db;
    db.set("s", "x");

    int added = 0;
    EXPECT_EQ(db.zadd("s", {{1.0, "a"}}, added), DBStatus::WrongType);

    double score = 0;
    EXPECT_EQ(db.zincrBy("s", "a", 1.0, score), DBStatus::WrongType);

    size_t len = 0;
    EXPECT_EQ(db.zcard("s", len), DBStatus::WrongType);

    size_t rank = 0;
    EXPECT_EQ(db.zrank("s", "a", false, rank), DBStatus::WrongType);

    std::vector<DBZSetEntry> out;
    EXPECT_EQ(db.zrangeByRank("s", 0, -1, false, out), DBStatus::WrongType);
    EXPECT_EQ(db.zrangeByScore("s", scoreRange(0, 1), false, out), DBStatus::WrongType);

    std::vector<DBZSetEntry> popped;
    EXPECT_EQ(db.zpopMin("s", 1, popped), DBStatus::WrongType);
    EXPECT_EQ(db.zpopMax("s", 1, popped), DBStatus::WrongType);
}

// ZINCRBY 对不存在的键要新建(Redis 语义:key 和 member 都会新建)
TEST(InMemoryDBTest, ZincrByCreatesKeyAndMember) {
    InMemoryDB db;
    double score = 0;
    ASSERT_EQ(db.zincrBy("z", "a", 10.0, score), DBStatus::OK);
    EXPECT_DOUBLE_EQ(score, 10.0);
    EXPECT_TRUE(db.exists("z"));

    ASSERT_EQ(db.zincrBy("z", "a", 5.0, score), DBStatus::OK);    // 只更新已有 member
    EXPECT_DOUBLE_EQ(score, 15.0);

    ASSERT_EQ(db.zincrBy("z", "b", 3.0, score), DBStatus::OK);    // 只新建 member
    EXPECT_DOUBLE_EQ(score, 3.0);

    size_t len = 0;
    ASSERT_EQ(db.zcard("z", len), DBStatus::OK);
    EXPECT_EQ(len, 2u);
}

// ZADD/ZINCRBY 是就地修改,已有 TTL 必须保留 —— 只有 SET 那种"整体替换值"才清 TTL
// 回归:曾经写成无条件 eraseExpire,把已有键的过期时间抹掉了
TEST(InMemoryDBTest, ZaddPreservesTtl) {
    InMemoryDB db;
    int added = 0;
    db.zadd("z", {{1.0, "a"}}, added);
    db.expire("z", 100);
    EXPECT_GT(db.ttl("z"), 0);

    db.zadd("z", {{2.0, "b"}}, added);
    EXPECT_GT(db.ttl("z"), 0);               // 不能被清成 -1

    double score = 0;
    db.zincrBy("z", "a", 1.0, score);
    EXPECT_GT(db.ttl("z"), 0);
}

// 弹空最后一个成员后键必须消失,且 TTL 记录要一起清掉
TEST(InMemoryDBTest, ZpopRemovesEmptyZSetKey) {
    InMemoryDB db;
    int added = 0;
    db.zadd("z", {{1.0, "a"}, {2.0, "b"}}, added);
    db.expire("z", 100);

    std::vector<DBZSetEntry> popped;
    ASSERT_EQ(db.zpopMin("z", 1, popped), DBStatus::OK);
    ASSERT_EQ(popped.size(), 1u);
    EXPECT_EQ(popped[0].member, "a");        // 最小分
    EXPECT_DOUBLE_EQ(popped[0].score, 1.0);
    EXPECT_TRUE(db.exists("z"));             // 还剩一个,键还在

    ASSERT_EQ(db.zpopMin("z", 1, popped), DBStatus::OK);
    ASSERT_EQ(popped.size(), 1u);
    EXPECT_EQ(popped[0].member, "b");
    EXPECT_FALSE(db.exists("z"));            // 弹空 → 键消失

    // TTL 记录没留下:同名键重建后不该继承旧过期时间
    db.zadd("z", {{5.0, "c"}}, added);
    EXPECT_EQ(db.ttl("z"), -1);
}

// count 超过元素个数:必须安全收工,不能读已释放的 zset
// 回归:曾经在循环里删键,下一轮 popMin 踩悬垂指针
TEST(InMemoryDBTest, ZpopMoreThanCardinality) {
    InMemoryDB db;
    int added = 0;
    db.zadd("z", {{1.0, "a"}, {2.0, "b"}}, added);

    std::vector<DBZSetEntry> popped;
    ASSERT_EQ(db.zpopMin("z", 999, popped), DBStatus::OK);
    ASSERT_EQ(popped.size(), 2u);            // 只吐出实际存在的两个
    EXPECT_EQ(membersOf(popped), (std::vector<std::string>{"a", "b"}));
    EXPECT_FALSE(db.exists("z"));

    db.zadd("z", {{1.0, "a"}}, added);
    ASSERT_EQ(db.zpopMax("z", 999, popped), DBStatus::OK);
    ASSERT_EQ(popped.size(), 1u);
    EXPECT_EQ(popped[0].member, "a");
    EXPECT_FALSE(db.exists("z"));
}

TEST(InMemoryDBTest, ZpopMaxTakesLargestFirst) {
    InMemoryDB db;
    int added = 0;
    db.zadd("z", {{1.0, "a"}, {5.0, "b"}, {3.0, "c"}}, added);

    std::vector<DBZSetEntry> popped;
    ASSERT_EQ(db.zpopMax("z", 2, popped), DBStatus::OK);
    ASSERT_EQ(popped.size(), 2u);
    EXPECT_EQ(membersOf(popped), (std::vector<std::string>{"b", "c"}));   // 分数从大到小

    size_t len = 0;
    ASSERT_EQ(db.zcard("z", len), DBStatus::OK);
    EXPECT_EQ(len, 1u);
}

// 成员名里的 '\0' 必须原样保留(二进制安全)
TEST(InMemoryDBTest, ZSetBinarySafeMember) {
    InMemoryDB db;
    const std::string bin("a\0b", 3);
    int added = 0;
    db.zadd("z", {{1.0, bin}}, added);

    double score = 0;
    ASSERT_EQ(db.zscore("z", bin, score), DBStatus::OK);
    EXPECT_DOUBLE_EQ(score, 1.0);
    EXPECT_EQ(db.zscore("z", "a", score), DBStatus::NotFound);   // 截断到 'a' 不该命中
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

TEST(InMemoryDBTest, SnapshotExportsZSet) {
    InMemoryDB db;
    int added = 0;
    db.zadd("z1", {{2.0, "b"}, {1.0, "a"}, {3.0, "c"}}, added);
    db.zadd("z2", {{1.0, "x"}}, added);
    db.expire("z2", 100);

    const auto snap = db.snapshot();
    ASSERT_EQ(snap.size(), 2u);

    const DBSnapshotEntry* z1 = findEntry(snap, "z1");
    ASSERT_NE(z1, nullptr);
    EXPECT_EQ(z1->type, redisObject::RedisObjectType::ZSET);
    EXPECT_EQ(z1->expiredAtMs, -1);
    EXPECT_TRUE(z1->stringValue.empty());
    EXPECT_TRUE(z1->hashEntries.empty());
    // 快照按分数升序导出(rangeByRank),AOF 回放后顺序可复现
    ASSERT_EQ(z1->zsetEntries.size(), 3u);
    EXPECT_DOUBLE_EQ(z1->zsetEntries[0].score, 1.0);
    EXPECT_EQ(z1->zsetEntries[0].member, "a");
    EXPECT_DOUBLE_EQ(z1->zsetEntries[1].score, 2.0);
    EXPECT_EQ(z1->zsetEntries[1].member, "b");
    EXPECT_DOUBLE_EQ(z1->zsetEntries[2].score, 3.0);
    EXPECT_EQ(z1->zsetEntries[2].member, "c");

    const DBSnapshotEntry* z2 = findEntry(snap, "z2");
    ASSERT_NE(z2, nullptr);
    EXPECT_GT(z2->expiredAtMs, 0);           // 带 TTL,存的是绝对时刻
}
