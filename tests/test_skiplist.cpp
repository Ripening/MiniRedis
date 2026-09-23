#include <gtest/gtest.h>
#include "core/skipList.hpp"

#include <algorithm>
#include <random>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using Entry = std::pair<double, std::string>;
using Model = std::vector<Entry>;   // 期望的升序序列

std::string elemOf(const SkipListNode* node) {
    return std::string(node->element().c_str(), node->element().len());
}

SDS sdsOf(const std::string& s) { return SDS(std::string_view(s)); }

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

// 沿 0 层 forward 链摊平(用 find 定位首节点,不经过 nodeByRank/rank);
// 顺带校验 backward 链,链尾必须落在 forward(0)==nullptr 的节点上
Model walkChain(const SkipList& sl, const Entry& first) {
    Model out;
    const SkipListNode* cur = sl.find(first.first, sdsOf(first.second));
    EXPECT_NE(cur, nullptr) << "起点元素都找不到";
    const SkipListNode* prev = nullptr;
    while (cur != nullptr) {
        EXPECT_EQ(cur->backward(), prev) << "backward 链在第 " << (out.size() + 1) << " 名断开";
        out.emplace_back(cur->score(), elemOf(cur));
        prev = cur;
        cur = cur->forward(0);
    }
    return out;
}

// 逐名探测,不依赖公开的 length 访问器
size_t lengthOf(const SkipList& sl) {
    size_t n = 0;
    while (sl.nodeByRank(n + 1) != nullptr) ++n;
    return n;
}

// 全量对账:长度、0 层链顺序、nodeByRank 逐名、rank 逐元素
void expectMatch(const SkipList& sl, const Model& expected) {
    EXPECT_EQ(lengthOf(sl), expected.size());
    if (!expected.empty()) {
        EXPECT_EQ(walkChain(sl, expected.front()), expected);
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        const SkipListNode* byRank = sl.nodeByRank(i + 1);
        ASSERT_NE(byRank, nullptr) << "nodeByRank(" << i + 1 << ") 为 null";
        EXPECT_EQ(byRank->score(), expected[i].first);
        EXPECT_EQ(elemOf(byRank), expected[i].second);
        EXPECT_EQ(sl.rank(expected[i].first, sdsOf(expected[i].second)), i + 1);
    }
    EXPECT_EQ(sl.nodeByRank(expected.size() + 1), nullptr);
}

Model toModel(const std::set<Entry>& s) { return Model(s.begin(), s.end()); }

}  // namespace

TEST(SkipListTest, InsertAndFind) {
    SkipList sl;
    sl.insert(1.5, SDS("a"));
    sl.insert(3.0, SDS("b"));

    const SkipListNode* a = sl.find(1.5, SDS("a"));
    ASSERT_NE(a, nullptr);
    EXPECT_DOUBLE_EQ(a->score(), 1.5);
    EXPECT_STREQ(a->element().c_str(), "a");

    EXPECT_NE(sl.find(3.0, SDS("b")), nullptr);
}

TEST(SkipListTest, FindMissingReturnsNull) {
    SkipList sl;
    sl.insert(1.5, SDS("a"));

    EXPECT_EQ(sl.find(9.9, SDS("a")), nullptr);   // 分数不存在
    EXPECT_EQ(sl.find(1.5, SDS("z")), nullptr);   // 分数在、成员不在
}

TEST(SkipListTest, SameScoreOrderedByElement) {
    SkipList sl;
    sl.insert(1.0, SDS("c"));
    sl.insert(1.0, SDS("a"));
    sl.insert(1.0, SDS("b"));
    sl.insert(2.0, SDS("a"));

    Model expected{{1.0, "a"}, {1.0, "b"}, {1.0, "c"}, {2.0, "a"}};
    expectMatch(sl, expected);

    EXPECT_EQ(sl.rank(1.0, SDS("a")), 1u);
    EXPECT_EQ(sl.rank(1.0, SDS("c")), 3u);
    EXPECT_EQ(sl.rank(2.0, SDS("a")), 4u);
}

TEST(SkipListTest, RankFollowsSortedOrderNotInsertOrder) {
    SkipList sl;
    const double scores[] = {5, 1, 3, 2, 4};
    for (double s : scores) {
        sl.insert(s, SDS("v"));
    }

    for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(sl.rank(static_cast<double>(i + 1), SDS("v")), static_cast<size_t>(i + 1))
            << "score " << i + 1;
    }
    EXPECT_EQ(sl.rank(6, SDS("v")), 0u);
}

TEST(SkipListTest, RankMissingReturnsZero) {
    SkipList sl;
    sl.insert(1.0, SDS("a"));
    sl.insert(2.0, SDS("b"));

    EXPECT_EQ(sl.rank(3.0, SDS("a")), 0u);   // 分数不存在
    EXPECT_EQ(sl.rank(1.0, SDS("z")), 0u);   // 成员不存在
}

TEST(SkipListTest, NodeByRankZeroAndOutOfRange) {
    SkipList sl;
    sl.insert(1.0, SDS("a"));
    sl.insert(2.0, SDS("b"));

    EXPECT_EQ(sl.nodeByRank(0), nullptr);   // rank 从 1 开始
    EXPECT_EQ(sl.nodeByRank(3), nullptr);
    EXPECT_EQ(sl.nodeByRank(100), nullptr);
}

TEST(SkipListTest, NodeByRankWalksInOrder) {
    SkipList sl;
    const int n = 50;
    for (int i = n; i >= 1; --i) {   // 逆序插入
        sl.insert(static_cast<double>(i), SDS("v" + std::to_string(i)));
    }

    Model expected;
    for (int i = 1; i <= n; ++i) {
        expected.emplace_back(static_cast<double>(i), "v" + std::to_string(i));
    }
    expectMatch(sl, expected);

    // 最后一名:forward(0) 必须是 nullptr(表尾)
    const SkipListNode* last = sl.nodeByRank(static_cast<size_t>(n));
    ASSERT_NE(last, nullptr);
    EXPECT_EQ(last->forward(0), nullptr);
    EXPECT_EQ(last->score(), static_cast<double>(n));
}

TEST(SkipListTest, DeleteExistingAndMissing) {
    SkipList sl;
    sl.insert(1.0, SDS("a"));
    sl.insert(2.0, SDS("b"));

    EXPECT_EQ(sl.Delete(3.0, SDS("a")), 0);   // 分数不存在
    EXPECT_EQ(sl.Delete(1.0, SDS("z")), 0);   // 成员不存在
    EXPECT_EQ(sl.Delete(1.0, SDS("a")), 1);
    EXPECT_EQ(sl.Delete(1.0, SDS("a")), 0);   // 重复删除
    EXPECT_EQ(sl.find(1.0, SDS("a")), nullptr);
    expectMatch(sl, Model{{2.0, "b"}});
}

TEST(SkipListTest, DeleteHeadShiftsRanks) {
    SkipList sl;
    for (int i = 1; i <= 5; ++i) {
        sl.insert(static_cast<double>(i), SDS("v"));
    }

    EXPECT_EQ(sl.Delete(1.0, SDS("v")), 1);

    Model expected;
    for (int i = 2; i <= 5; ++i) {
        expected.emplace_back(static_cast<double>(i), "v");
    }
    expectMatch(sl, expected);
    EXPECT_EQ(sl.nodeByRank(1)->score(), 2.0);
    EXPECT_EQ(sl.nodeByRank(1)->backward(), nullptr);
}

TEST(SkipListTest, DeleteTailUpdatesTailPointer) {
    SkipList sl;
    for (int i = 1; i <= 5; ++i) {
        sl.insert(static_cast<double>(i), SDS("v"));
    }

    EXPECT_EQ(sl.Delete(5.0, SDS("v")), 1);
    EXPECT_EQ(sl.find(5.0, SDS("v")), nullptr);

    // hasInRange 读的是 tail_->score(),删尾后上界应立即收到 4
    EXPECT_TRUE(sl.hasInRange(closed(0, 10)));
    EXPECT_FALSE(sl.hasInRange(closed(4.5, 5)));

    // 删尾之后还能正常追加新表尾
    sl.insert(6.0, SDS("v"));
    EXPECT_EQ(sl.lastInRange(closed(0, 100))->score(), 6.0);
    EXPECT_EQ(lengthOf(sl), 5u);
}

TEST(SkipListTest, DeleteMiddleKeepsRanksConsistent) {
    SkipList sl;
    std::set<Entry> model;
    for (int i = 1; i <= 10; ++i) {
        const Entry e{static_cast<double>(i), "v"};
        sl.insert(e.first, SDS("v"));
        model.insert(e);
    }

    for (int i : {4, 7, 1, 10, 5}) {   // 含头、尾、中间
        EXPECT_EQ(sl.Delete(static_cast<double>(i), SDS("v")), 1);
        EXPECT_EQ(model.erase(Entry{static_cast<double>(i), "v"}), 1u);
        expectMatch(sl, toModel(model));
    }
}

TEST(SkipListTest, DeleteAllLeavesEmptyThenReusable) {
    SkipList sl;
    const double order[] = {3, 1, 5, 2, 4};
    for (double s : order) {
        sl.insert(s, SDS("v"));
    }
    for (double s : order) {
        EXPECT_EQ(sl.Delete(s, SDS("v")), 1);
    }

    EXPECT_EQ(lengthOf(sl), 0u);
    EXPECT_EQ(sl.nodeByRank(1), nullptr);
    EXPECT_EQ(sl.find(1.0, SDS("v")), nullptr);
    EXPECT_EQ(sl.rank(1.0, SDS("v")), 0u);
    EXPECT_EQ(sl.Delete(1.0, SDS("v")), 0);

    // 清空后重建,原有层结构不能拖后腿
    for (int i = 1; i <= 3; ++i) {
        sl.insert(static_cast<double>(i), SDS("n"));
    }
    expectMatch(sl, Model{{1.0, "n"}, {2.0, "n"}, {3.0, "n"}});
}

TEST(SkipListTest, EmptyListQueries) {
    SkipList sl;

    EXPECT_EQ(sl.find(1.0, SDS("a")), nullptr);
    EXPECT_EQ(sl.rank(1.0, SDS("a")), 0u);
    EXPECT_EQ(sl.nodeByRank(1), nullptr);
    EXPECT_EQ(sl.Delete(1.0, SDS("a")), 0);
    EXPECT_FALSE(sl.hasInRange(closed(0, 10)));
    EXPECT_EQ(sl.firstInRange(closed(0, 10)), nullptr);
    EXPECT_EQ(sl.lastInRange(closed(0, 10)), nullptr);
}

TEST(SkipListTest, HasInRangeRejectsInvalidRange) {
    SkipList sl;
    sl.insert(2.0, SDS("x"));

    EXPECT_FALSE(sl.hasInRange(closed(3, 2)));      // min > max
    EXPECT_FALSE(sl.hasInRange(openMax(2, 2)));     // [2,2)
    EXPECT_FALSE(sl.hasInRange(openMin(2, 2)));     // (2,2]
    EXPECT_FALSE(sl.hasInRange(openBoth(2, 2)));    // (2,2)
    EXPECT_TRUE(sl.hasInRange(closed(2, 2)));       // [2,2] 单点闭区间
}

TEST(SkipListTest, HasInRangeBounds) {
    SkipList sl;
    for (double s : {1.0, 3.0, 5.0}) {
        sl.insert(s, SDS("v"));
    }

    EXPECT_TRUE(sl.hasInRange(closed(1, 5)));
    EXPECT_TRUE(sl.hasInRange(closed(0, 1)));      // 上界贴住最小值
    EXPECT_TRUE(sl.hasInRange(closed(5, 9)));      // 下界贴住最大值
    EXPECT_FALSE(sl.hasInRange(closed(0, 0.5)));   // 整体在区间之上
    EXPECT_FALSE(sl.hasInRange(closed(5.5, 10)));  // 整体在区间之下
    EXPECT_FALSE(sl.hasInRange(openMax(0, 1)));    // 最大值被排除
    EXPECT_FALSE(sl.hasInRange(openMin(5, 9)));    // 最小值被排除
}

// hasInRange 只比对"表头/表尾"这个粗筛:区间落在两元素之间的缝隙时它会误报 true,
// 真正兜底的是 first/lastInRange 的收尾判断
TEST(SkipListTest, RangeGapIsCoarseFiltered) {
    SkipList sl;
    sl.insert(3.0, SDS("v"));
    sl.insert(5.0, SDS("v"));

    EXPECT_TRUE(sl.hasInRange(closed(4, 4)));
    EXPECT_EQ(sl.firstInRange(closed(4, 4)), nullptr);
    EXPECT_EQ(sl.lastInRange(closed(4, 4)), nullptr);

    // 开区间 (3,5) 中间同样没有元素
    EXPECT_TRUE(sl.hasInRange(openBoth(3, 5)));
    EXPECT_EQ(sl.firstInRange(openBoth(3, 5)), nullptr);
    EXPECT_EQ(sl.lastInRange(openBoth(3, 5)), nullptr);
}

TEST(SkipListTest, FirstAndLastInRange) {
    SkipList sl;
    for (double s : {1.0, 3.0, 5.0, 7.0, 9.0}) {
        sl.insert(s, SDS("v"));
    }

    EXPECT_EQ(sl.firstInRange(closed(3, 7))->score(), 3.0);
    EXPECT_EQ(sl.lastInRange(closed(3, 7))->score(), 7.0);

    EXPECT_EQ(sl.firstInRange(openMin(3, 7))->score(), 5.0);
    EXPECT_EQ(sl.lastInRange(openMin(3, 7))->score(), 7.0);

    EXPECT_EQ(sl.firstInRange(openMax(3, 7))->score(), 3.0);
    EXPECT_EQ(sl.lastInRange(openMax(3, 7))->score(), 5.0);

    EXPECT_EQ(sl.firstInRange(openBoth(3, 7))->score(), 5.0);
    EXPECT_EQ(sl.lastInRange(openBoth(3, 7))->score(), 5.0);
}

TEST(SkipListTest, RangeOutsideAllElements) {
    SkipList sl;
    for (double s : {1.0, 3.0, 5.0}) {
        sl.insert(s, SDS("v"));
    }

    EXPECT_EQ(sl.firstInRange(closed(10, 20)), nullptr);
    EXPECT_EQ(sl.lastInRange(closed(10, 20)), nullptr);
    EXPECT_EQ(sl.firstInRange(closed(-10, 0)), nullptr);
    EXPECT_EQ(sl.lastInRange(closed(-10, 0)), nullptr);
}

// 同分组内的首尾由成员字典序决定,与 rank 顺序一致
TEST(SkipListTest, RangeWithinSameScoreGroup) {
    SkipList sl;
    sl.insert(1.0, SDS("c"));
    sl.insert(1.0, SDS("a"));
    sl.insert(1.0, SDS("b"));
    sl.insert(2.0, SDS("a"));

    const SkipListNode* first = sl.firstInRange(closed(1, 1));
    ASSERT_NE(first, nullptr);
    EXPECT_STREQ(first->element().c_str(), "a");

    const SkipListNode* last = sl.lastInRange(closed(1, 1));
    ASSERT_NE(last, nullptr);
    EXPECT_STREQ(last->element().c_str(), "c");
}

TEST(SkipListTest, BinarySafeElement) {
    SkipList sl;
    const char raw[] = {'a', '\0', 'b'};
    const std::string_view bin(raw, 3);

    sl.insert(1.0, SDS("ab"));
    sl.insert(1.0, SDS(bin));

    // memcmp 语义:'\0' < 'b',含空字节的成员排在前面
    EXPECT_EQ(sl.rank(1.0, SDS(bin)), 1u);
    EXPECT_EQ(sl.rank(1.0, SDS("ab")), 2u);

    const SkipListNode* node = sl.find(1.0, SDS(bin));
    ASSERT_NE(node, nullptr);
    EXPECT_EQ(node->element().len(), 3u);
    EXPECT_EQ(elemOf(node), std::string(raw, 3));
    EXPECT_EQ(sl.find(1.0, SDS("a")), nullptr);   // 截断到 'a' 不应命中
}

TEST(SkipListTest, StressMatchesReferenceModel) {
    SkipList sl;
    std::set<Entry> model;
    std::mt19937 rng(12345);
    std::uniform_int_distribution<int> scoreDist(1, 6);   // 分数刻意压窄,制造大量同分
    std::uniform_int_distribution<int> nameDist(0, 60);

    auto gen = [&]() {
        return Entry{static_cast<double>(scoreDist(rng)), "m" + std::to_string(nameDist(rng))};
    };

    std::vector<Entry> inserted;
    for (int i = 0; i < 300; ++i) {
        const Entry e = gen();
        if (!model.insert(e).second) continue;   // 跳表不保证重复插入语义,生成时去重
        sl.insert(e.first, sdsOf(e.second));
        inserted.push_back(e);
    }
    ASSERT_GE(inserted.size(), 100u) << "随机数据去重后太少,失去测试意义";
    expectMatch(sl, toModel(model));

    // 随机删掉一半,过程中抽检
    std::shuffle(inserted.begin(), inserted.end(), rng);
    std::vector<Entry> removed;
    for (const Entry& e : inserted) {
        if (removed.size() >= inserted.size() / 2) break;
        EXPECT_EQ(sl.Delete(e.first, sdsOf(e.second)), 1);
        EXPECT_EQ(model.erase(e), 1u);
        removed.push_back(e);
        if (removed.size() % 40 == 0) {
            expectMatch(sl, toModel(model));
        }
    }
    expectMatch(sl, toModel(model));

    ASSERT_FALSE(removed.empty());
    EXPECT_EQ(sl.Delete(removed.front().first, sdsOf(removed.front().second)), 0);

    // 清空
    for (const Entry& e : toModel(model)) {
        EXPECT_EQ(sl.Delete(e.first, sdsOf(e.second)), 1);
    }
    EXPECT_EQ(lengthOf(sl), 0u);
    EXPECT_EQ(sl.nodeByRank(1), nullptr);
    EXPECT_EQ(sl.firstInRange(closed(-1e9, 1e9)), nullptr);
    EXPECT_FALSE(sl.hasInRange(closed(-1e9, 1e9)));
}
