#pragma once

#include "core/dict.hpp"
#include "core/skipList.hpp"
#include <string_view>
#include <vector>
#include <optional>
#include <utility>
class ZSet
{
public:
    
    // 添加元素 （插入新元素返回true,修改score返回false）
    bool add(std::string_view member,double score);
    // 查找 score ,不存在 返回null
    const double* scoreOf(std::string_view member) const;
    // 删除元素
    bool remove(std::string_view member);
    // 返回成员数量
    size_t card() const;
    // 成员名次:1-based(最小分=1),reverse=true 时逆序(最大分=1);不存在返回 0
    size_t rankOf(std::string_view member, bool reverse = false) const;
    // 按名次区间返回节点(ZRANGE/ZREVRANGE 地基);start/stop 可负、可越界,内部归一化
    std::vector<const SkipListNode*> rangeByRank(long long start, long long stop, bool reverse = false) const;
    // 按分数区间返回节点(ZRANGEBYSCORE 地基);顺序按跳表序,reverse 时从大分往小分
    std::vector<const SkipListNode*> rangeByScore(const ScoreRange& range, bool reverse = false) const;
    // 分数增量(ZINCRBY):不存在按 0 起算,返回新分数
    double incrBy(std::string_view member, double delta);
    // 弹出最小分成员(ZPOPMIN):空表返回 nullopt,否则 (member, score)
    std::optional<std::pair<SDS, double>> popMin();
    // 弹出最大分成员(ZPOPMAX)
    std::optional<std::pair<SDS, double>> popMax();
private:
    DICT<double>    dict_;
    SkipList        skiplist_;
};

