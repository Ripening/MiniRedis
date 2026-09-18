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
