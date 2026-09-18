#include <gtest/gtest.h>
#include "core/sds.hpp"

#include <cstring>
#include <string>

TEST(SdsTest, ConstructAndRead) {
    SDS s("hello");
    EXPECT_EQ(s.len(), 5u);
    EXPECT_STREQ(s.c_str(), "hello");
}

TEST(SdsTest, EmptyDefault) {
    SDS s;
    EXPECT_EQ(s.len(), 0u);
    EXPECT_STREQ(s.c_str(), "");
}

TEST(SdsTest, AppendGrows) {
    SDS s("abc");
    s.append("def");
    EXPECT_EQ(s.len(), 6u);
    EXPECT_STREQ(s.c_str(), "abcdef");
    EXPECT_GE(s.capacity(), s.len());
}

TEST(SdsTest, AppendManyTimes) {
    SDS s;
    for (int i = 0; i < 1000; ++i) {
        s.append("x");
    }
    EXPECT_EQ(s.len(), 1000u);
    EXPECT_EQ(std::strlen(s.c_str()), 1000u);
}

TEST(SdsTest, Clear) {
    SDS s("hello");
    s.clear();
    EXPECT_EQ(s.len(), 0u);
    EXPECT_STREQ(s.c_str(), "");
}

TEST(SdsTest, MoveTransfersOwnership) {
    SDS a("hello");
    SDS b(std::move(a));
    EXPECT_STREQ(b.c_str(), "hello");
    EXPECT_EQ(b.len(), 5u);
}
