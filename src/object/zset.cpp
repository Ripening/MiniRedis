#include "object/zset.hpp"

bool ZSet::add(std::string_view member,double score){
    const double* cur = dict_.get(SDS(member));

    if (cur == nullptr) {
        skiplist_.insert(score, SDS(member));
        dict_.set(SDS(member), score);
        return true;
    }
    if (*cur == score) return false;   // 分数没变,跳表不用动

    skiplist_.Delete(*cur, SDS(member));
    skiplist_.insert(score, SDS(member));
    dict_.set(SDS(member), score);
    return false;
}

const double* ZSet::scoreOf(std::string_view member) const{
    const double* score = dict_.get(SDS(member));
    return score;
}

bool ZSet::remove(std::string_view member){
    const double* score = scoreOf(member);
    if(score == nullptr) return false;
    skiplist_.Delete(*score,SDS(member));
    dict_.erase(SDS(member));
    return true;
}

size_t ZSet::card() const{
    return dict_.size();
}

size_t ZSet::rankOf(std::string_view member, bool reverse) const{
    const double* score = dict_.get(SDS(member));
    if (score == nullptr) return 0;                     // 不存在,0 作哨兵

    size_t rank = skiplist_.rank(*score, SDS(member));  // 正序 1-based
    if (reverse) rank = skiplist_.length() - rank + 1;  // 镜像成逆序名次
    return rank;
}

std::vector<const SkipListNode*> ZSet::rangeByRank(long long start, long long stop, bool reverse) const{
    const long long L = static_cast<long long>(skiplist_.length());

    // 负数从尾数起、越界截断、空区间退出  ← 前面五轮讲的全部在这
    if (start < 0) start += L;
    if (stop  < 0) stop  += L;
    if (start < 0) start = 0;
    if (start > stop || start >= L) return {};
    if (stop >= L) stop = L - 1;

      // 起点与方向:正序第 s 个 = rank s+1;逆序第 s 个 = rank L-s
    const size_t beginRank = reverse ? static_cast<size_t>(L - start)
                                       : static_cast<size_t>(start + 1);
    const SkipListNode* cur = skiplist_.nodeByRank(beginRank);

    std::vector<const SkipListNode*> out;
    const long long steps = stop - start;
    for (long long i = 0; i <= steps; ++i) {
        out.push_back(cur);
        cur = reverse ? cur->backward() : cur->forward(0);
    }
    return out;
}

std::vector<const SkipListNode*> ZSet::rangeByScore(const ScoreRange& range, bool reverse) const{

    std::vector<const SkipListNode*> out;
    if(reverse){
        SkipListNode* current = skiplist_.lastInRange(range);
        while(current && range.gteMin(current->score())){
            out.push_back(current);
            current = current->backward();
        }
    }
    else{
        SkipListNode* current = skiplist_.firstInRange(range);
        while(current && range.lteMax(current->score())){
            out.push_back(current);
            current = current->forward(0);
        }
    }

    return out;
}

double ZSet::incrBy(std::string_view member, double delta){
    const double* score = dict_.get(SDS(member));
    double newScore = (score == nullptr ? 0.0 : *score) + delta;
    add(member, newScore);      // 复用 add:判重和跳表更新不必重写
    return newScore;            // ZINCRBY 的回复就是新分数
}

std::optional<std::pair<SDS, double>> ZSet::popMin(){
    const SkipListNode* node = skiplist_.nodeByRank(1);     // 最小分 = 正序 rank 1
    if (node == nullptr) return std::nullopt;               // 空表

    SDS member(std::string_view(node->element().c_str(), node->element().len()));  // ① 先拷
    double score = node->score();

    remove(std::string_view(member.c_str(), member.len())); // ② 再删
    return std::make_pair(std::move(member), score);
}

std::optional<std::pair<SDS, double>> ZSet::popMax(){
    const SkipListNode* node = skiplist_.nodeByRank(skiplist_.length());  // 最大分 = 正序 rank L
    if (node == nullptr) return std::nullopt;                             // 空表(rank=0 已兜)

    SDS member(std::string_view(node->element().c_str(), node->element().len()));
    double score = node->score();

    remove(std::string_view(member.c_str(), member.len()));
    return std::make_pair(std::move(member), score);
}

