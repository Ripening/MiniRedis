#include <gtest/gtest.h>
#include "object/zset.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Entry = std::pair<double, std::string>;   // (score, member),即跳表序
using Model = std::map<std::string, double>;    // member -> score

std::string elemOf(const SkipListNode* node) {
    return std::string(node->element().c_str(), node->element().len());
}

std::string strOf(const SDS& s) { return std::string(s.c_str(), s.len()); }

std::vector<Entry> entriesOf(const std::vector<const SkipListNode*>& nodes) {
    std::vector<Entry> out;
    for (const SkipListNode* n : nodes) {
        out.emplace_back(n->score(), elemOf(n));
    }
    return out;
}

std::vector<Entry> sortedModel(const Model& model) {
    std::vector<Entry> out;
    for (const auto& kv : model) {
        out.emplace_back(kv.second, kv.first);
    }
    std::sort(out.begin(), out.end());
    return out;
}

ScoreRange closed(double min, double max) { return ScoreRange{min, max}; }

ScoreRange openMin(double min, double max) {
    ScoreRange r{min, max};
    r.minExclusive = true;
    return r;
}

ScoreRange openMax(double min, double max) {
    ScoreRange r{min, max};
    r.maxExclusive = true;
    return r;
}

ScoreRange openBoth(double min, double max) {
    ScoreRange r{min, max};
    r.minExclusive = true;
    r.maxExclusive = true;
    return r;
}

// 双索引全量对账:card、正/反序区间、逐成员 scoreOf/rankOf 与模型一致
void expectMatch(const ZSet& zs, const Model& model) {
    EXPECT_EQ(zs.card(), model.size());

    const std::vector<Entry> expect = sortedModel(model);

    EXPECT_EQ(entriesOf(zs.rangeByRank(0, -1)), expect);
    EXPECT_EQ(entriesOf(zs.rangeByScore(closed(-1e9, 1e9))), expect);

    const std::vector<Entry> revExpect(expect.rbegin(), expect.rend());
    EXPECT_EQ(entriesOf(zs.rangeByRank(0, -1, true)), revExpect);
    EXPECT_EQ(entriesOf(zs.rangeByScore(closed(-1e9, 1e9), true)), revExpect);

    for (size_t i = 0; i < expect.size(); ++i) {
        const std::string& member = expect[i].second;
        const double* sc = zs.scoreOf(member);
        ASSERT_NE(sc, nullptr) << member;
        EXPECT_DOUBLE_EQ(*sc, expect[i].first) << member;
        EXPECT_EQ(zs.rankOf(member), i + 1) << member;
        EXPECT_EQ(zs.rankOf(member, true), expect.size() - i) << member;
    }
}

}  // namespace

TEST(ZSetTest, AddInsertUpdateAndSameScore) {
    ZSet zs;
    EXPECT_TRUE(zs.add("a", 1.0));    // 新成员
    EXPECT_FALSE(zs.add("a", 2.0));   // 改分
    EXPECT_FALSE(zs.add("a", 2.0));   // 同分短路

    const double* sc = zs.scoreOf("a");
    ASSERT_NE(sc, nullptr);
    EXPECT_DOUBLE_EQ(*sc, 2.0);
    EXPECT_EQ(zs.card(), 1u);
}

TEST(ZSetTest, ScoreOfMissingReturnsNull) {
    ZSet zs;
    zs.add("a", 1.0);
    EXPECT_EQ(zs.scoreOf("b"), nullptr);

    ZSet empty;
    EXPECT_EQ(empty.scoreOf("a"), nullptr);
}

TEST(ZSetTest, RemoveExistingAndMissing) {
    ZSet zs;
    zs.add("a", 1.0);
    zs.add("b", 2.0);

    EXPECT_TRUE(zs.remove("a"));
    EXPECT_FALSE(zs.remove("a"));   // 重复删除
    EXPECT_FALSE(zs.remove("z"));   // 从未存在
    EXPECT_EQ(zs.scoreOf("a"), nullptr);
    EXPECT_EQ(zs.card(), 1u);
    EXPECT_EQ(zs.rankOf("a"), 0u);
}

TEST(ZSetTest, RankOfOrderAndReverseMirror) {
    ZSet zs;
    for (int i = 1; i <= 5; ++i) {
        zs.add("m" + std::to_string(i), i);
    }

    for (size_t i = 0; i < 5; ++i) {
        const std::string m = "m" + std::to_string(i + 1);
        EXPECT_EQ(zs.rankOf(m), i + 1);
        EXPECT_EQ(zs.rankOf(m, true), 5 - i);   // 镜像:正序第 k = 逆序第 L-k+1
    }
    EXPECT_EQ(zs.rankOf("m3", true), 3u);       // 中间点两向对称

    EXPECT_EQ(zs.rankOf("z"), 0u);              // 不存在
    EXPECT_EQ(zs.rankOf("z", true), 0u);
}

TEST(ZSetTest, RangeByRankBasics) {
    ZSet zs;
    for (int i = 1; i <= 5; ++i) {
        zs.add("m" + std::to_string(i), i);
    }

    const std::vector<Entry> all{{1, "m1"}, {2, "m2"}, {3, "m3"}, {4, "m4"}, {5, "m5"}};
    EXPECT_EQ(entriesOf(zs.rangeByRank(0, -1)), all);
    EXPECT_EQ(entriesOf(zs.rangeByRank(0, 0)), (std::vector<Entry>{{1, "m1"}}));
    EXPECT_EQ(entriesOf(zs.rangeByRank(-1, -1)), (std::vector<Entry>{{5, "m5"}}));
    EXPECT_EQ(entriesOf(zs.rangeByRank(-2, -1)), (std::vector<Entry>{{4, "m4"}, {5, "m5"}}));
    EXPECT_EQ(entriesOf(zs.rangeByRank(1, 3)), (std::vector<Entry>{{2, "m2"}, {3, "m3"}, {4, "m4"}}));
}

TEST(ZSetTest, RangeByRankOutOfBoundsAndEmptyInterval) {
    ZSet zs;
    for (int i = 1; i <= 5; ++i) {
        zs.add("m" + std::to_string(i), i);
    }

    EXPECT_EQ(zs.rangeByRank(0, 999).size(), 5u);     // stop 超尾:截到表尾
    EXPECT_EQ(zs.rangeByRank(100, 200).size(), 0u);   // start 超尾:空
    EXPECT_EQ(zs.rangeByRank(3, 1).size(), 0u);       // 倒置:空
    EXPECT_EQ(zs.rangeByRank(-1, -5).size(), 0u);     // 换算后倒置:空
    EXPECT_EQ(zs.rangeByRank(-100, 0).size(), 1u);    // start 超头:夹到 0
    EXPECT_EQ(zs.rangeByRank(-100, 2).size(), 3u);
}

TEST(ZSetTest, RangeByRankReverse) {
    ZSet zs;
    for (int i = 1; i <= 5; ++i) {
        zs.add("m" + std::to_string(i), i);
    }

    const std::vector<Entry> rev{{5, "m5"}, {4, "m4"}, {3, "m3"}, {2, "m2"}, {1, "m1"}};
    EXPECT_EQ(entriesOf(zs.rangeByRank(0, -1, true)), rev);
    EXPECT_EQ(entriesOf(zs.rangeByRank(0, 0, true)), (std::vector<Entry>{{5, "m5"}}));
    EXPECT_EQ(entriesOf(zs.rangeByRank(-2, -1, true)), (std::vector<Entry>{{2, "m2"}, {1, "m1"}}));
}

TEST(ZSetTest, RangeByRankOnEmptySet) {
    ZSet zs;
    EXPECT_TRUE(zs.rangeByRank(0, -1).empty());
    EXPECT_TRUE(zs.rangeByRank(-1, 0).empty());
    EXPECT_TRUE(zs.rangeByRank(0, 0, true).empty());
}

TEST(ZSetTest, RangeByScoreClosedOpenBounds) {
    ZSet zs;
    for (int i = 1; i <= 5; ++i) {
        zs.add("m" + std::to_string(i), i);
    }

    const std::vector<Entry> mid{{2, "m2"}, {3, "m3"}, {4, "m4"}};
    EXPECT_EQ(entriesOf(zs.rangeByScore(closed(2, 4))), mid);
    EXPECT_EQ(entriesOf(zs.rangeByScore(openBoth(2, 4))), (std::vector<Entry>{{3, "m3"}}));
    EXPECT_EQ(entriesOf(zs.rangeByScore(openMin(2, 4))), (std::vector<Entry>{{3, "m3"}, {4, "m4"}}));
    EXPECT_EQ(entriesOf(zs.rangeByScore(openMax(2, 4))), (std::vector<Entry>{{2, "m2"}, {3, "m3"}}));
    EXPECT_EQ(entriesOf(zs.rangeByScore(closed(2, 2))), (std::vector<Entry>{{2, "m2"}}));

    EXPECT_TRUE(zs.rangeByScore(closed(10, 20)).empty());
    EXPECT_TRUE(zs.rangeByScore(closed(3.5, 3.6)).empty());   // 缝隙
}

TEST(ZSetTest, RangeByScoreReverse) {
    ZSet zs;
    for (int i = 1; i <= 5; ++i) {
        zs.add("m" + std::to_string(i), i);
    }

    const std::vector<Entry> rev{{4, "m4"}, {3, "m3"}, {2, "m2"}};
    EXPECT_EQ(entriesOf(zs.rangeByScore(closed(2, 4), true)), rev);
}

TEST(ZSetTest, RangeByScoreEmptySet) {
    ZSet zs;
    EXPECT_TRUE(zs.rangeByScore(closed(-10, 10)).empty());
    EXPECT_TRUE(zs.rangeByScore(closed(-10, 10), true).empty());
}

TEST(ZSetTest, IncrByMissingStartsFromZero) {
    ZSet zs;
    EXPECT_DOUBLE_EQ(zs.incrBy("a", 2.5), 2.5);

    const double* sc = zs.scoreOf("a");
    ASSERT_NE(sc, nullptr);
    EXPECT_DOUBLE_EQ(*sc, 2.5);
    EXPECT_EQ(zs.card(), 1u);
}

TEST(ZSetTest, IncrByAccumulates) {
    ZSet zs;
    zs.add("a", 1.0);

    EXPECT_DOUBLE_EQ(zs.incrBy("a", 0.5), 1.5);
    EXPECT_DOUBLE_EQ(zs.incrBy("a", -2.0), -0.5);   // 分数可为负
    EXPECT_DOUBLE_EQ(zs.incrBy("a", 0.0), -0.5);    // 增 0 不动

    EXPECT_EQ(zs.card(), 1u);
    EXPECT_EQ(zs.rankOf("a"), 1u);
}

TEST(ZSetTest, PopMinRemovesSmallest) {
    ZSet zs;
    zs.add("a", 1.0);
    zs.add("b", 2.0);
    zs.add("c", 3.0);

    auto r = zs.popMin();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(strOf(r->first), "a");
    EXPECT_DOUBLE_EQ(r->second, 1.0);

    EXPECT_EQ(zs.card(), 2u);
    EXPECT_EQ(zs.scoreOf("a"), nullptr);
    EXPECT_EQ(zs.rankOf("b"), 1u);   // 删后名次前移
}

TEST(ZSetTest, PopMaxRemovesLargest) {
    ZSet zs;
    zs.add("a", 1.0);
    zs.add("b", 2.0);
    zs.add("c", 3.0);

    auto r = zs.popMax();
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(strOf(r->first), "c");
    EXPECT_DOUBLE_EQ(r->second, 3.0);

    EXPECT_EQ(zs.card(), 2u);
    EXPECT_EQ(zs.scoreOf("c"), nullptr);
    EXPECT_EQ(zs.rankOf("b"), 2u);
}

TEST(ZSetTest, PopDrainsInOrderThenNullopt) {
    ZSet zs;
    for (int i = 1; i <= 3; ++i) {
        zs.add("m" + std::to_string(i), i);
    }

    for (int i = 1; i <= 3; ++i) {   // popMin 按升序依次吐出
        auto r = zs.popMin();
        ASSERT_TRUE(r.has_value());
        EXPECT_EQ(strOf(r->first), "m" + std::to_string(i));
        EXPECT_DOUBLE_EQ(r->second, static_cast<double>(i));
    }

    EXPECT_EQ(zs.card(), 0u);
    EXPECT_FALSE(zs.popMin().has_value());   // 空表
    EXPECT_FALSE(zs.popMax().has_value());
}

TEST(ZSetTest, UpdateScoreMovesRank) {
    ZSet zs;
    zs.add("a", 1.0);
    zs.add("b", 2.0);
    zs.add("c", 3.0);

    EXPECT_EQ(zs.rankOf("a"), 1u);
    zs.add("a", 4.0);                // 抬高分数:a 变最大
    EXPECT_EQ(zs.rankOf("a"), 3u);
    EXPECT_EQ(zs.rankOf("a", true), 1u);
    EXPECT_EQ(zs.rankOf("b"), 1u);   // 其余成员名次前移

    expectMatch(zs, Model{{"a", 4.0}, {"b", 2.0}, {"c", 3.0}});
}

TEST(ZSetTest, SameScoreOrderedByMember) {
    ZSet zs;
    zs.add("c", 1.0);
    zs.add("a", 1.0);
    zs.add("b", 1.0);

    const std::vector<Entry> expect{{1, "a"}, {1, "b"}, {1, "c"}};
    EXPECT_EQ(entriesOf(zs.rangeByRank(0, -1)), expect);
    EXPECT_EQ(zs.rankOf("a"), 1u);
    EXPECT_EQ(zs.rankOf("c"), 3u);
}

TEST(ZSetTest, BinarySafeMember) {
    ZSet zs;
    const char raw[] = {'a', '\0', 'b'};
    const std::string_view bin(raw, 3);

    zs.add(bin, 1.0);
    zs.add("ab", 2.0);

    const double* sc = zs.scoreOf(bin);
    ASSERT_NE(sc, nullptr);
    EXPECT_DOUBLE_EQ(*sc, 1.0);
    EXPECT_EQ(zs.scoreOf("a"), nullptr);   // 截断到 'a' 不应命中

    auto r = zs.popMin();                  // pop 的拷贝路径也要保留完整字节
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(r->first.len(), 3u);
    EXPECT_EQ(strOf(r->first), std::string(raw, 3));
}

TEST(ZSetTest, StressAgainstModel) {
    ZSet zs;
    Model model;
    std::mt19937 rng(20260922);
    std::uniform_int_distribution<int> opDist(0, 3);
    std::uniform_int_distribution<int> nameDist(0, 40);
    std::uniform_int_distribution<int> scoreDist(1, 200);

    for (int i = 0; i < 2000; ++i) {
        const std::string member = "m" + std::to_string(nameDist(rng));
        switch (opDist(rng)) {
            case 0:
            case 1: {   // add 占一半
                const double score = scoreDist(rng);
                zs.add(member, score);
                model[member] = score;
                break;
            }
            case 2: {   // incrBy(整数增量,避免浮点误差干扰对账)
                const double delta = scoreDist(rng);
                EXPECT_DOUBLE_EQ(zs.incrBy(member, delta), model[member] + delta);
                model[member] += delta;
                break;
            }
            case 3: {
                EXPECT_EQ(zs.remove(member), model.erase(member) == 1);
                break;
            }
        }
        if (i % 200 == 0) {
            expectMatch(zs, model);
        }
    }
    expectMatch(zs, model);
}
