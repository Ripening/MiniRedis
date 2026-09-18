#include <gtest/gtest.h>
#include "protocol/respEncoder.hpp"
#include "protocol/respParser.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string bulk(const std::string& s) {
    return "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
}

std::string command(const std::vector<std::string>& args) {
    std::string out = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& a : args) {
        out += bulk(a);
    }
    return out;
}

// 把完整报文在每个位置切成两次 feed 测试:
// 前半段必须返回 false(数据不够),补齐后必须成功,且缓冲区恰好消费干净。
void expectIncremental(const std::string& wire) {
    for (size_t k = 0; k <= wire.size(); ++k) {
        RespParser parser;
        RespObject out;
        parser.feed(wire.data(), k);
        if (k < wire.size()) {
            EXPECT_FALSE(parser.parse(out)) << "split at " << k;
        }
        parser.feed(wire.data() + k, wire.size() - k);
        EXPECT_TRUE(parser.parse(out)) << "split at " << k;
        EXPECT_EQ(parser.pendingBytes(), 0u) << "split at " << k;
    }
}

} // namespace

// ---------- 五种类型的正常解析 ----------

TEST(RespParserTest, ParseSimpleString) {
    RespParser parser;
    RespObject out;
    const std::string wire = "+OK\r\n";
    parser.feed(wire.data(), wire.size());

    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RespType::SIMPLE_STRING);
    EXPECT_EQ(out.str, "OK");
}

TEST(RespParserTest, ParseErrorReply) {
    RespParser parser;
    RespObject out;
    const std::string wire = "-ERR unknown command\r\n";
    parser.feed(wire.data(), wire.size());

    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RespType::ERROR);
    EXPECT_EQ(out.str, "ERR unknown command");
}

TEST(RespParserTest, ParseIntegerIncludesNegative) {
    RespParser parser;
    RespObject out;

    const std::string pos = ":42\r\n";
    parser.feed(pos.data(), pos.size());
    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RespType::INTEGER);
    EXPECT_EQ(out.integer, 42);

    const std::string neg = ":-5\r\n";
    parser.feed(neg.data(), neg.size());
    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.integer, -5);
}

TEST(RespParserTest, ParseNullBulk) {
    RespParser parser;
    RespObject out;
    const std::string wire = "$-1\r\n";
    parser.feed(wire.data(), wire.size());

    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RespType::NULL_BULK);
}

TEST(RespParserTest, ParseEmptyBulk) {
    RespParser parser;
    RespObject out;
    const std::string wire = "$0\r\n\r\n";
    parser.feed(wire.data(), wire.size());

    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RespType::BULK_STRING);
    EXPECT_TRUE(out.str.empty());
}

TEST(RespParserTest, ParseCommandArray) {
    RespParser parser;
    RespObject out;
    const std::string wire = command({"SET", "mykey", "hello"});
    parser.feed(wire.data(), wire.size());

    ASSERT_TRUE(parser.parse(out));
    ASSERT_EQ(out.type, RespType::ARRAY);
    ASSERT_EQ(out.elements.size(), 3u);
    EXPECT_EQ(out.elements[0].type, RespType::BULK_STRING);
    EXPECT_EQ(out.elements[0].str, "SET");
    EXPECT_EQ(out.elements[1].str, "mykey");
    EXPECT_EQ(out.elements[2].str, "hello");
}

// 二进制安全:载荷里带 \r\n 和 \0 也必须原样取出
TEST(RespParserTest, BinarySafeBulkWithCrlfAndNul) {
    const std::string payload("a\r\nb\0c", 6);
    const std::string wire = bulk(payload);

    RespParser parser;
    RespObject out;
    parser.feed(wire.data(), wire.size());

    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RespType::BULK_STRING);
    EXPECT_EQ(out.str.size(), 6u);
    EXPECT_EQ(out.str, payload);
}

// ---------- 增量解析(半包)与粘包 ----------

TEST(RespParserTest, IncrementalEverySplitPointCommand) {
    expectIncremental(command({"SET", "mykey", "hello"}));
}

TEST(RespParserTest, IncrementalEverySplitPointSimpleString) {
    expectIncremental("+OK\r\n");
}

// 长度声明跨包:先到 "$5\r\nhel",补齐 "lo\r\n" 后才能解析
TEST(RespParserTest, PartialBulkContentAcrossFeeds) {
    RespParser parser;
    RespObject out;

    const std::string part1 = "$5\r\nhel";
    parser.feed(part1.data(), part1.size());
    EXPECT_FALSE(parser.parse(out));

    const std::string part2 = "lo\r\n";
    parser.feed(part2.data(), part2.size());
    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.str, "hello");
}

TEST(RespParserTest, PipelineTwoCommandsOneFeed) {
    const std::string first = command({"GET", "a"});
    const std::string second = command({"SET", "b", "1"});
    const std::string wire = first + second;

    RespParser parser;
    RespObject out;
    parser.feed(wire.data(), wire.size());

    ASSERT_TRUE(parser.parse(out));
    ASSERT_EQ(out.type, RespType::ARRAY);
    ASSERT_EQ(out.elements.size(), 2u);
    EXPECT_EQ(out.elements[0].str, "GET");
    EXPECT_EQ(out.elements[1].str, "a");
    EXPECT_EQ(parser.pendingBytes(), second.size());

    ASSERT_TRUE(parser.parse(out));
    ASSERT_EQ(out.elements.size(), 3u);
    EXPECT_EQ(out.elements[0].str, "SET");
    EXPECT_EQ(parser.pendingBytes(), 0u);

    EXPECT_FALSE(parser.parse(out));   // 没数据了
}

// 数组分两段到达:第一段不够必须 false(整体回滚),补齐后从头重建
TEST(RespParserTest, PartialArrayThenComplete) {
    const std::string part1 = "*3\r\n" + bulk("a") + "$2\r\nx";
    const std::string part2 = "y\r\n" + bulk("c");

    RespParser parser;
    RespObject out;
    parser.feed(part1.data(), part1.size());
    EXPECT_FALSE(parser.parse(out));
    EXPECT_EQ(parser.pendingBytes(), part1.size());   // 回滚:一个字节都没消费

    parser.feed(part2.data(), part2.size());
    ASSERT_TRUE(parser.parse(out));
    ASSERT_EQ(out.elements.size(), 3u);
    EXPECT_EQ(out.elements[0].str, "a");
    EXPECT_EQ(out.elements[1].str, "xy");
    EXPECT_EQ(out.elements[2].str, "c");
}

// 同一个 out 连续解析不同类型:上一次的字段必须被清干净,不能串数据
TEST(RespParserTest, OutObjectResetBetweenParses) {
    RespParser parser;
    RespObject out;

    const std::string s = "+OK\r\n";
    parser.feed(s.data(), s.size());
    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RespType::SIMPLE_STRING);

    const std::string i = ":7\r\n";
    parser.feed(i.data(), i.size());
    ASSERT_TRUE(parser.parse(out));
    EXPECT_EQ(out.type, RespType::INTEGER);
    EXPECT_EQ(out.integer, 7);
    EXPECT_TRUE(out.str.empty());
    EXPECT_TRUE(out.elements.empty());
}

// 对端声明 100 万个元素但只发来几个字节:必须 false 等待,不能按声明预分配
TEST(RespParserTest, HugeArrayCountDoesNotAllocate) {
    RespParser parser;
    RespObject out;
    const std::string wire = "*1000000\r\n";
    parser.feed(wire.data(), wire.size());

    EXPECT_FALSE(parser.parse(out));
    EXPECT_EQ(parser.pendingBytes(), wire.size());
}

// ---------- 非法报文必须抛异常(不可恢复,区别于半包的 false) ----------

TEST(RespParserTest, ThrowsOnInvalidPrefix) {
    RespParser parser;
    RespObject out;
    const std::string wire = "@oops\r\n";
    parser.feed(wire.data(), wire.size());
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

TEST(RespParserTest, ThrowsOnInvalidArrayCount) {
    RespParser parser;
    RespObject out;
    const std::string wire = "*abc\r\n";
    parser.feed(wire.data(), wire.size());
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

// 数字超 long long 范围(out_of_range 分支)
TEST(RespParserTest, ThrowsOnIntegerOverflow) {
    RespParser parser;
    RespObject out;
    const std::string wire = ":99999999999999999999\r\n";
    parser.feed(wire.data(), wire.size());
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

// 数字后带尾随垃圾(parsed != line.size() 分支)
TEST(RespParserTest, ThrowsOnTrailingGarbageInInteger) {
    RespParser parser;
    RespObject out;
    const std::string wire = ":42abc\r\n";
    parser.feed(wire.data(), wire.size());
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

TEST(RespParserTest, ThrowsOnBulkLenBelowMinusOne) {
    RespParser parser;
    RespObject out;
    const std::string wire = "$-2\r\n";
    parser.feed(wire.data(), wire.size());
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

TEST(RespParserTest, ThrowsOnNullArray) {
    RespParser parser;
    RespObject out;
    const std::string wire = "*-1\r\n";
    parser.feed(wire.data(), wire.size());
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

// 长度前缀撒谎(声明 6 实际内容 5):消费完声明长度后帧边界对不上,
// 必须在当场抛错,而不是把下一条命令的字节吃掉造成静默串位
TEST(RespParserTest, DesyncDetectedWhenLengthLies) {
    RespParser parser;
    RespObject out;
    const std::string wire = "$6\r\nhello\r\n" + command({"PING"});
    parser.feed(wire.data(), wire.size());
    EXPECT_THROW(parser.parse(out), std::runtime_error);
}

// ---------- 编码器 ----------

TEST(RespEncoderTest, FormatAllTypes) {
    EXPECT_EQ(RespEncoder::simpleString("OK"), "+OK\r\n");
    EXPECT_EQ(RespEncoder::error("ERR bad"), "-ERR bad\r\n");
    EXPECT_EQ(RespEncoder::integer(42), ":42\r\n");
    EXPECT_EQ(RespEncoder::integer(-5), ":-5\r\n");
    EXPECT_EQ(RespEncoder::nullBulk(), "$-1\r\n");
    EXPECT_EQ(RespEncoder::bulkString("hello"), "$5\r\nhello\r\n");
    EXPECT_EQ(RespEncoder::bulkString(""), "$0\r\n\r\n");
    EXPECT_EQ(RespEncoder::array({"a", "bb"}), "*2\r\n$1\r\na\r\n$2\r\nbb\r\n");
}

// 编码 → 解析往返:编码结果必须能被自己的解析器原样读回
TEST(RespEncoderTest, EncodeThenParseRoundTrip) {
    const std::vector<std::string> payloads = {
        "hello",
        "",
        "a\r\nb",
        std::string("bin\0ary", 7),
    };

    for (const auto& p : payloads) {
        RespParser parser;
        RespObject out;
        const std::string wire = RespEncoder::bulkString(p);
        parser.feed(wire.data(), wire.size());

        ASSERT_TRUE(parser.parse(out)) << "payload size " << p.size();
        EXPECT_EQ(out.type, RespType::BULK_STRING);
        EXPECT_EQ(out.str, p);
        EXPECT_EQ(parser.pendingBytes(), 0u);
    }
}
