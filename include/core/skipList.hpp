#pragma once

#include "core/skiplistnode.hpp"

#include <string>
#include <vector>
#include <random>

constexpr int kMaxLevel = 32;    //跳表最高32层


struct ScoreRange{
    double min;
    double max;
    bool minExclusive = false;  //最小值 除外吗？ 等价于开区间
    bool maxExclusive = false;  //最大值 除外吗？
    bool gteMin(double score) const {
        if(minExclusive) return score > min;
        else return score >= min;
    }
    bool lteMax(double score) const {
        if(maxExclusive) return score < max;
        else return score <= max;
    }
};

class SkipList
{
public:
    SkipList();
    ~SkipList();
    SkipList(const SkipList&) = delete;
    SkipList& operator=(const SkipList&) = delete;
    
    // 返回跳表长度
    size_t length() const { return length_;}

    //跳表中插入数据
    void insert(double score, SDS element);
    //删除
    int Delete(double score,SDS element);
    //查找
    const SkipListNode* find(double score, const SDS& element) const;

    // (score,element) --> rank
    size_t rank(double score, const SDS& element) const;
    // rank --> (score,element)
    const SkipListNode* nodeByRank(size_t rank) const;

    //区间组
    bool hasInRange(const ScoreRange& range) const;
    
    SkipListNode* firstInRange(const ScoreRange& range) const;
    SkipListNode* lastInRange(const ScoreRange& range) const;
private:
    int getRandomLevel();
    void deleteNode(SkipListNode* node,SkipListNode** update);
private:
    SkipListNode*   header_;    //头节点
    SkipListNode*   tail_;      //尾节点
    size_t          length_;    //跳表长度
    int             level_;     //当前最高层数  

    std::mt19937    rng_;
    std::bernoulli_distribution coin_;
};  

