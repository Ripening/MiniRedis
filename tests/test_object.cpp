#include <gtest/gtest.h>
#include "object/redisObject.hpp"

#include <memory>
#include <string>

// 本文件真正让编译器实例化 DICT<SDS>:
// createHashObject / getHashObjectValue 在此前没有任何调用者

TEST(RedisObjectTest, CreateStringAndRead) {
    auto obj = redisObject::createStringObject("hello");
    const SDS* v = redisObject::getStringObjectValue(obj.get());
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v->c_str(), "hello");
    EXPECT_EQ(v->len(), 5u);
}

TEST(RedisObjectTest, StringMutableAccessorLetsAppend) {
    auto obj = redisObject::createStringObject("abc");
    SDS* v = redisObject::getStringObjectValue(obj.get());
    ASSERT_NE(v, nullptr);
    v->append("def");
    EXPECT_STREQ(redisObject::getStringObjectValue(obj.get())->c_str(), "abcdef");
}

TEST(RedisObjectTest, CreateHashStartsEmpty) {
    auto obj = redisObject::createHashObject();
    const DICT<SDS>* h = redisObject::getHashObjectValue(obj.get());
    ASSERT_NE(h, nullptr);
    EXPECT_EQ(h->size(), 0u);
}

TEST(RedisObjectTest, HashSetAndFieldLookup) {
    auto obj = redisObject::createHashObject();
    DICT<SDS>* h = redisObject::getHashObjectValue(obj.get());
    ASSERT_NE(h, nullptr);

    h->set(SDS("field1"), SDS("value1"));
    h->set(SDS("field2"), SDS("value2"));

    EXPECT_EQ(h->size(), 2u);
    const SDS* v = h->get(SDS("field1"));
    ASSERT_NE(v, nullptr);
    EXPECT_STREQ(v->c_str(), "value1");
    EXPECT_EQ(h->get(SDS("missing")), nullptr);
}

TEST(RedisObjectTest, HashOverwriteField) {
    auto obj = redisObject::createHashObject();
    DICT<SDS>* h = redisObject::getHashObjectValue(obj.get());

    h->set(SDS("f"), SDS("v1"));
    h->set(SDS("f"), SDS("v2"));

    EXPECT_EQ(h->size(), 1u);
    EXPECT_STREQ(h->get(SDS("f"))->c_str(), "v2");
}

// 类型不匹配必须返回 nullptr(命令层 WRONGTYPE 判断的基础)
TEST(RedisObjectTest, WrongTypeAccessReturnsNull) {
    auto str = redisObject::createStringObject("x");
    EXPECT_EQ(redisObject::getHashObjectValue(str.get()), nullptr);

    auto hash = redisObject::createHashObject();
    EXPECT_EQ(redisObject::getStringObjectValue(hash.get()), nullptr);
}

// Hash 内层扩大(rehash)后,原有字段仍可读
TEST(RedisObjectTest, HashSurvivesRehash) {
    auto obj = redisObject::createHashObject();
    DICT<SDS>* h = redisObject::getHashObjectValue(obj.get());

    const int n = 100;
    for (int i = 0; i < n; ++i) {
        h->set(SDS("f" + std::to_string(i)), SDS("v" + std::to_string(i)));
    }
    EXPECT_EQ(h->size(), static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const SDS* v = h->get(SDS("f" + std::to_string(i)));
        ASSERT_NE(v, nullptr) << "missing field " << i;
        EXPECT_STREQ(v->c_str(), ("v" + std::to_string(i)).c_str());
    }
}

// unique_ptr 析构 → ~redisObject → switch → delete DICT<SDS> → 逐节点析构 SDS
TEST(RedisObjectTest, DestroyHashReleasesPayload) {
    auto obj = redisObject::createHashObject();
    DICT<SDS>* h = redisObject::getHashObjectValue(obj.get());
    for (int i = 0; i < 64; ++i) {
        h->set(SDS("k" + std::to_string(i)), SDS("v" + std::to_string(i)));
    }
    obj.reset();   // 不崩即通过
    SUCCEED();
}
