#include <gtest/gtest.h>
#include "command/commandDispatcher.hpp"

#include <string>
#include <vector>

namespace {

const std::string kWrongType =
    "-WRONGTYPE Operation against a key holding the wrong kind of value\r\n";

std::string run(CommandDispatcher& dispatcher, std::vector<std::string> argv) {
    return dispatcher.dispatch(argv);
}

long long intValue(const std::string& reply) {
    return std::stoll(reply.substr(1, reply.size() - 2));
}

} // namespace

TEST(CommandDispatcherTest, EmptyAndUnknownAndCaseInsensitive) {
    CommandDispatcher d;
    EXPECT_EQ(run(d, {}), "-ERR empty command\r\n");
    EXPECT_EQ(run(d, {"nope"}), "-ERR unknown command 'nope'\r\n");
    EXPECT_EQ(run(d, {"PING"}), "+PONG\r\n");
    EXPECT_EQ(run(d, {"ping"}), "+PONG\r\n");
    EXPECT_EQ(run(d, {"PiNg"}), "+PONG\r\n");
}

TEST(CommandDispatcherTest, WrongArity) {
    CommandDispatcher d;
    EXPECT_EQ(run(d, {"GET"}), "-ERR wrong number of arguments for 'get' command\r\n");
    EXPECT_EQ(run(d, {"SET", "k"}), "-ERR wrong number of arguments for 'set' command\r\n");
    EXPECT_EQ(run(d, {"EXISTS"}), "-ERR wrong number of arguments for 'exists' command\r\n");
    EXPECT_EQ(run(d, {"MSET", "a", "1", "b"}), "-ERR wrong number of arguments for 'mset' command\r\n");
    EXPECT_EQ(run(d, {"HSET", "h", "f", "v", "f2"}), "-ERR wrong number of arguments for 'hset' command\r\n");
}

TEST(CommandDispatcherTest, SetGetRoundTrip) {
    CommandDispatcher d;
    EXPECT_EQ(run(d, {"SET", "k", "hello"}), "+OK\r\n");
    EXPECT_EQ(run(d, {"GET", "k"}), "$5\r\nhello\r\n");
    EXPECT_EQ(run(d, {"GET", "missing"}), "$-1\r\n");
}

TEST(CommandDispatcherTest, MSetMGet) {
    CommandDispatcher d;
    EXPECT_EQ(run(d, {"MSET", "a", "1", "b", "2"}), "+OK\r\n");
    EXPECT_EQ(run(d, {"MGET", "a", "b", "c"}), "*3\r\n$1\r\n1\r\n$1\r\n2\r\n$-1\r\n");
}

TEST(CommandDispatcherTest, DelAndExists) {
    CommandDispatcher d;
    run(d, {"MSET", "a", "1", "b", "2"});
    EXPECT_EQ(run(d, {"EXISTS", "a", "b", "c"}), ":2\r\n");
    EXPECT_EQ(run(d, {"DEL", "a", "nope"}), ":1\r\n");
    EXPECT_EQ(run(d, {"EXISTS", "a", "b"}), ":1\r\n");
}

TEST(CommandDispatcherTest, IncrAndIncrBy) {
    CommandDispatcher d;
    EXPECT_EQ(run(d, {"INCR", "n"}), ":1\r\n");
    EXPECT_EQ(run(d, {"INCR", "n"}), ":2\r\n");
    EXPECT_EQ(run(d, {"INCRBY", "n", "10"}), ":12\r\n");
    EXPECT_EQ(run(d, {"INCRBY", "n", "-20"}), ":-8\r\n");

    run(d, {"SET", "s", "abc"});
    EXPECT_EQ(run(d, {"INCR", "s"}), "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(run(d, {"INCRBY", "n", "abc"}), "-ERR value is not an integer or out of range\r\n");

    run(d, {"SET", "big", "9223372036854775807"});
    EXPECT_EQ(run(d, {"INCR", "big"}), "-ERR increment or decrement would overflow\r\n");
}

TEST(CommandDispatcherTest, IncrOnHashIsWrongType) {
    CommandDispatcher d;
    run(d, {"HSET", "h", "f", "v"});
    EXPECT_EQ(run(d, {"INCR", "h"}), kWrongType);
}

TEST(CommandDispatcherTest, HashBasic) {
    CommandDispatcher d;
    EXPECT_EQ(run(d, {"HSET", "h", "f1", "v1"}), ":1\r\n");
    EXPECT_EQ(run(d, {"HSET", "h", "f1", "v2"}), ":0\r\n");   // 覆盖已有 field 不算新增
    EXPECT_EQ(run(d, {"HSET", "h", "f2", "a", "f3", "b"}), ":2\r\n");
    EXPECT_EQ(run(d, {"HGET", "h", "f1"}), "$2\r\nv2\r\n");
    EXPECT_EQ(run(d, {"HGET", "h", "nope"}), "$-1\r\n");
    EXPECT_EQ(run(d, {"HGET", "missing", "f"}), "$-1\r\n");
    EXPECT_EQ(run(d, {"HEXISTS", "h", "f1"}), ":1\r\n");
    EXPECT_EQ(run(d, {"HEXISTS", "h", "nope"}), ":0\r\n");
    EXPECT_EQ(run(d, {"HLEN", "h"}), ":3\r\n");
    EXPECT_EQ(run(d, {"HLEN", "missing"}), ":0\r\n");
}

TEST(CommandDispatcherTest, HGetAllSortedFlatArray) {
    CommandDispatcher d;
    run(d, {"HSET", "h", "b", "2", "a", "1"});
    EXPECT_EQ(run(d, {"HGETALL", "h"}), "*4\r\n$1\r\na\r\n$1\r\n1\r\n$1\r\nb\r\n$1\r\n2\r\n");
    EXPECT_EQ(run(d, {"HGETALL", "missing"}), "*0\r\n");
}

TEST(CommandDispatcherTest, HDelDeletesEmptiedHash) {
    CommandDispatcher d;
    run(d, {"HSET", "h", "f1", "a", "f2", "b"});
    EXPECT_EQ(run(d, {"HDEL", "h", "f1", "nope"}), ":1\r\n");
    EXPECT_EQ(run(d, {"HDEL", "h", "f2"}), ":1\r\n");
    EXPECT_EQ(run(d, {"EXISTS", "h"}), ":0\r\n");             // hash 删空后 key 一起消失
    EXPECT_EQ(run(d, {"HDEL", "missing", "f"}), ":0\r\n");
}

TEST(CommandDispatcherTest, WrongTypeAcrossStringAndHash) {
    CommandDispatcher d;
    run(d, {"SET", "k", "v"});
    EXPECT_EQ(run(d, {"HSET", "k", "f", "v"}), kWrongType);
    EXPECT_EQ(run(d, {"HGET", "k", "f"}), kWrongType);
    EXPECT_EQ(run(d, {"HLEN", "k"}), kWrongType);
    EXPECT_EQ(run(d, {"HGETALL", "k"}), kWrongType);

    run(d, {"HSET", "h", "f", "v"});
    EXPECT_EQ(run(d, {"GET", "h"}), kWrongType);
}

TEST(CommandDispatcherTest, ExpireTtlPersist) {
    CommandDispatcher d;
    run(d, {"SET", "k", "v"});

    EXPECT_EQ(run(d, {"TTL", "k"}), ":-1\r\n");
    EXPECT_EQ(run(d, {"TTL", "missing"}), ":-2\r\n");

    EXPECT_EQ(run(d, {"EXPIRE", "k", "100"}), ":1\r\n");
    const long long ttl = intValue(run(d, {"TTL", "k"}));
    EXPECT_GE(ttl, 99);
    EXPECT_LE(ttl, 100);

    const long long pttl = intValue(run(d, {"PTTL", "k"}));
    EXPECT_GT(pttl, 99000);
    EXPECT_LE(pttl, 100000);

    EXPECT_EQ(run(d, {"PERSIST", "k"}), ":1\r\n");
    EXPECT_EQ(run(d, {"PERSIST", "k"}), ":0\r\n");
    EXPECT_EQ(run(d, {"TTL", "k"}), ":-1\r\n");
}

TEST(CommandDispatcherTest, ExpireEdgeCases) {
    CommandDispatcher d;
    run(d, {"SET", "k", "v"});
    EXPECT_EQ(run(d, {"EXPIRE", "missing", "10"}), ":0\r\n");
    EXPECT_EQ(run(d, {"EXPIRE", "k", "abc"}), "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(run(d, {"EXPIRE", "k", "0"}), ":1\r\n");        // 非正数 = 立即删除
    EXPECT_EQ(run(d, {"EXISTS", "k"}), ":0\r\n");
}

TEST(CommandDispatcherTest, ZAddAndZRange) {
    CommandDispatcher d;
    EXPECT_EQ(run(d, {"ZADD", "z", "2", "b", "1", "a", "3", "c"}), ":3\r\n");
    EXPECT_EQ(run(d, {"ZADD", "z", "5", "a"}), ":0\r\n");     // 改分不算新增
    EXPECT_EQ(run(d, {"ZRANGE", "z", "0", "-1"}), "*3\r\n$1\r\nb\r\n$1\r\nc\r\n$1\r\na\r\n");
    EXPECT_EQ(run(d, {"ZRANGE", "z", "0", "1"}), "*2\r\n$1\r\nb\r\n$1\r\nc\r\n");
    EXPECT_EQ(run(d, {"ZREVRANGE", "z", "0", "-1"}), "*3\r\n$1\r\na\r\n$1\r\nc\r\n$1\r\nb\r\n");
    EXPECT_EQ(run(d, {"ZRANGE", "missing", "0", "-1"}), "*0\r\n");
}

TEST(CommandDispatcherTest, ZAddScoreParsing) {
    CommandDispatcher d;
    EXPECT_EQ(run(d, {"ZADD", "z", "inf", "top", "-inf", "bottom"}), ":2\r\n");
    EXPECT_EQ(run(d, {"ZRANGE", "z", "0", "-1"}), "*2\r\n$6\r\nbottom\r\n$3\r\ntop\r\n");
    EXPECT_EQ(run(d, {"ZRANGEBYSCORE", "z", "-inf", "+inf"}), "*2\r\n$6\r\nbottom\r\n$3\r\ntop\r\n");
    EXPECT_EQ(run(d, {"ZSCORE", "z", "top"}), "$3\r\ninf\r\n");

    EXPECT_EQ(run(d, {"ZADD", "z", "abc", "m"}), "-ERR value is not a valid float\r\n");
    EXPECT_EQ(run(d, {"ZADD", "z", "1", "m", "2"}), "-ERR wrong number of arguments for 'zadd' command\r\n");
}

TEST(CommandDispatcherTest, ZRangeByRankEdges) {
    CommandDispatcher d;
    run(d, {"ZADD", "z", "1", "a", "2", "b", "3", "c"});
    EXPECT_EQ(run(d, {"ZRANGE", "z", "-2", "-1"}), "*2\r\n$1\r\nb\r\n$1\r\nc\r\n");
    EXPECT_EQ(run(d, {"ZRANGE", "z", "5", "10"}), "*0\r\n");
    EXPECT_EQ(run(d, {"ZRANGE", "z", "abc", "1"}), "-ERR value is not an integer or out of range\r\n");
}

TEST(CommandDispatcherTest, ZRangeByScoreBounds) {
    CommandDispatcher d;
    run(d, {"ZADD", "z", "1", "a", "2", "b", "3", "c"});
    EXPECT_EQ(run(d, {"ZRANGEBYSCORE", "z", "1", "2"}), "*2\r\n$1\r\na\r\n$1\r\nb\r\n");
    EXPECT_EQ(run(d, {"ZRANGEBYSCORE", "z", "(1", "3"}), "*2\r\n$1\r\nb\r\n$1\r\nc\r\n");
    // 反向命令客户端传的是 max min,写反了会静默回空数组
    EXPECT_EQ(run(d, {"ZREVRANGEBYSCORE", "z", "3", "1"}), "*3\r\n$1\r\nc\r\n$1\r\nb\r\n$1\r\na\r\n");
    EXPECT_EQ(run(d, {"ZREVRANGEBYSCORE", "z", "+inf", "-inf"}), "*3\r\n$1\r\nc\r\n$1\r\nb\r\n$1\r\na\r\n");
    EXPECT_EQ(run(d, {"ZRANGEBYSCORE", "z", "abc", "1"}), "-ERR min or max is not a float\r\n");
}

TEST(CommandDispatcherTest, ZRangeWithScores) {
    CommandDispatcher d;
    run(d, {"ZADD", "z", "1", "a", "2.5", "b"});

    const std::string pairs = "*4\r\n$1\r\na\r\n$1\r\n1\r\n$1\r\nb\r\n$3\r\n2.5\r\n";
    EXPECT_EQ(run(d, {"ZRANGE", "z", "0", "-1", "WITHSCORES"}), pairs);
    EXPECT_EQ(run(d, {"zrange", "z", "0", "-1", "withscores"}), pairs);   // 选项大小写不敏感
    EXPECT_EQ(run(d, {"ZRANGEBYSCORE", "z", "-inf", "+inf", "WITHSCORES"}), pairs);

    const std::string reversed = "*4\r\n$1\r\nb\r\n$3\r\n2.5\r\n$1\r\na\r\n$1\r\n1\r\n";
    EXPECT_EQ(run(d, {"ZREVRANGE", "z", "0", "-1", "WITHSCORES"}), reversed);
    EXPECT_EQ(run(d, {"ZREVRANGEBYSCORE", "z", "+inf", "-inf", "WITHSCORES"}), reversed);

    // 尾参数拼错或多余必须报语法错,不能静默当成不带分数
    EXPECT_EQ(run(d, {"ZRANGE", "z", "0", "-1", "WITHSCORZ"}), "-ERR syntax error\r\n");
    EXPECT_EQ(run(d, {"ZRANGE", "z", "0", "-1", "WITHSCORES", "EXTRA"}), "-ERR syntax error\r\n");
    EXPECT_EQ(run(d, {"ZPOPMIN", "z", "1", "WITHSCORES"}), "-ERR syntax error\r\n");
}

TEST(CommandDispatcherTest, ZRankZScoreZCard) {
    CommandDispatcher d;
    run(d, {"ZADD", "z", "1", "a", "2", "b", "3", "c"});
    EXPECT_EQ(run(d, {"ZRANK", "z", "a"}), ":0\r\n");
    EXPECT_EQ(run(d, {"ZRANK", "z", "c"}), ":2\r\n");
    EXPECT_EQ(run(d, {"ZREVRANK", "z", "a"}), ":2\r\n");
    EXPECT_EQ(run(d, {"ZRANK", "z", "nope"}), "$-1\r\n");      // 找不到回 nil,不是 -1
    EXPECT_EQ(run(d, {"ZRANK", "missing", "a"}), "$-1\r\n");
    EXPECT_EQ(run(d, {"ZSCORE", "z", "b"}), "$1\r\n2\r\n");
    EXPECT_EQ(run(d, {"ZSCORE", "z", "nope"}), "$-1\r\n");
    EXPECT_EQ(run(d, {"ZCARD", "z"}), ":3\r\n");
    EXPECT_EQ(run(d, {"ZCARD", "missing"}), ":0\r\n");
}

TEST(CommandDispatcherTest, ZIncrBy) {
    CommandDispatcher d;
    EXPECT_EQ(run(d, {"ZINCRBY", "z", "1.5", "m"}), "$3\r\n1.5\r\n");
    EXPECT_EQ(run(d, {"ZINCRBY", "z", "1", "m"}), "$3\r\n2.5\r\n");
    EXPECT_EQ(run(d, {"ZINCRBY", "z", "-2.5", "m"}), "$1\r\n0\r\n");   // 归零后成员仍在
    EXPECT_EQ(run(d, {"ZCARD", "z"}), ":1\r\n");
    EXPECT_EQ(run(d, {"ZINCRBY", "z", "abc", "m"}), "-ERR value is not a valid float\r\n");
}

TEST(CommandDispatcherTest, ZPopMinMax) {
    CommandDispatcher d;
    run(d, {"ZADD", "z", "1", "a", "2", "b", "3", "c"});
    EXPECT_EQ(run(d, {"ZPOPMIN", "z"}), "*2\r\n$1\r\na\r\n$1\r\n1\r\n");   // 不带 count = 平铺
    EXPECT_EQ(run(d, {"ZPOPMAX", "z", "2"}),                              // 带 count = 嵌套
              "*2\r\n*2\r\n$1\r\nc\r\n$1\r\n3\r\n*2\r\n$1\r\nb\r\n$1\r\n2\r\n");
    EXPECT_EQ(run(d, {"EXISTS", "z"}), ":0\r\n");                          // 弹空后 key 一起消失
    EXPECT_EQ(run(d, {"ZPOPMIN", "z"}), "*0\r\n");
}

TEST(CommandDispatcherTest, ZPopCountErrors) {
    CommandDispatcher d;
    run(d, {"ZADD", "z", "1", "a"});
    EXPECT_EQ(run(d, {"ZPOPMIN", "z", "-1"}), "-ERR value is out of range, must be positive\r\n");
    EXPECT_EQ(run(d, {"ZPOPMIN", "z", "abc"}), "-ERR value is not an integer or out of range\r\n");
    EXPECT_EQ(run(d, {"ZPOPMIN", "z", "999"}), "*1\r\n*2\r\n$1\r\na\r\n$1\r\n1\r\n");
}

TEST(CommandDispatcherTest, ZSetWrongType) {
    CommandDispatcher d;
    run(d, {"SET", "k", "v"});
    EXPECT_EQ(run(d, {"ZADD", "k", "1", "m"}), kWrongType);
    EXPECT_EQ(run(d, {"ZRANGE", "k", "0", "-1"}), kWrongType);
    EXPECT_EQ(run(d, {"ZSCORE", "k", "m"}), kWrongType);
    EXPECT_EQ(run(d, {"ZCARD", "k"}), kWrongType);
    EXPECT_EQ(run(d, {"ZPOPMIN", "k"}), kWrongType);

    run(d, {"ZADD", "z", "1", "m"});
    EXPECT_EQ(run(d, {"GET", "z"}), kWrongType);
}

TEST(CommandDispatcherTest, ZAddPreservesTtl) {
    CommandDispatcher d;
    run(d, {"ZADD", "z", "1", "a"});
    EXPECT_EQ(run(d, {"EXPIRE", "z", "100"}), ":1\r\n");
    run(d, {"ZADD", "z", "2", "b"});                 // 就地修改,不该清掉过期时间
    EXPECT_GE(intValue(run(d, {"TTL", "z"})), 99);
}
