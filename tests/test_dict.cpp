#include <gtest/gtest.h>
#include "core/dict.hpp"

#include <string>

TEST(DictTest, SetAndGet) {
    DICT<SDS> d;
    d.set(SDS("name"), SDS("alice"));

    const SDS* v = d.get(SDS("name"));
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v->c_str(), "alice");
}

TEST(DictTest, GetMissingReturnsNull) {
    DICT<SDS> d;
    EXPECT_EQ(d.get(SDS("nope")), nullptr);
}

TEST(DictTest, SetExistingKeyUpdates) {
    DICT<SDS> d;
    d.set(SDS("k"), SDS("v1"));
    d.set(SDS("k"), SDS("v2"));

    EXPECT_EQ(d.size(), 1u);
    const SDS* v = d.get(SDS("k"));
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v->c_str(), "v2");
}

TEST(DictTest, Erase) {
    DICT<SDS> d;
    d.set(SDS("k"), SDS("v"));

    EXPECT_TRUE(d.erase(SDS("k")));
    EXPECT_EQ(d.get(SDS("k")), nullptr);
    EXPECT_EQ(d.size(), 0u);
    EXPECT_FALSE(d.erase(SDS("k")));
}

TEST(DictTest, SizeCounts) {
    DICT<SDS> d;
    EXPECT_EQ(d.size(), 0u);
    for (int i = 0; i < 5; ++i) {
        d.set(SDS("k" + std::to_string(i)), SDS("v"));
    }
    EXPECT_EQ(d.size(), 5u);
}

// 初始表大小 8,插到第 8 个触发扩容并进入渐进式 rehash;
// 后续每次操作搬一个 bucket,这里持续读写直到 rehash 走完
TEST(DictTest, ResizeAndRehashKeepsData) {
    DICT<SDS> d;
    const int n = 200;
    for (int i = 0; i < n; ++i) {
        d.set(SDS("key" + std::to_string(i)), SDS("val" + std::to_string(i)));
    }
    EXPECT_EQ(d.size(), static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const SDS* v = d.get(SDS("key" + std::to_string(i)));
        ASSERT_NE(v, nullptr) << "missing key " << i;
        EXPECT_STREQ(v->c_str(), ("val" + std::to_string(i)).c_str());
    }
}

TEST(DictTest, ForEachVisitsAll) {
    DICT<SDS> d;
    d.set(SDS("a"), SDS("1"));
    d.set(SDS("b"), SDS("2"));
    d.set(SDS("c"), SDS("3"));

    size_t count = 0;
    d.forEach([&count](const SDS& key, const SDS& value) {
        EXPECT_GT(key.len(), 0u);
        EXPECT_GT(value.len(), 0u);
        ++count;
    });
    EXPECT_EQ(count, 3u);
}

TEST(DictTest, MoveConstructTransfersEntries) {
    DICT<SDS> a;
    a.set(SDS("k"), SDS("v"));

    DICT<SDS> b(std::move(a));
    const SDS* v = b.get(SDS("k"));
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v->c_str(), "v");
}

TEST(DictTest, MoveAssignTransfersEntries) {
    DICT<SDS> a;
    a.set(SDS("k"), SDS("v"));

    DICT<SDS> b;
    b.set(SDS("old"), SDS("gone"));
    b = std::move(a);

    EXPECT_EQ(b.get(SDS("old")), nullptr);
    const SDS* v = b.get(SDS("k"));
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v->c_str(), "v");
}
