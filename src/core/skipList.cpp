#include "core/skipList.hpp"

namespace{
    bool precedes(const SkipListNode& node,double score,const SDS& element){
        if (node.score() != score) return node.score() < score;
        return node.element().compare(element) < 0;    // 第二级:字典序
    }
}

SkipList::SkipList():
    header_(new SkipListNode(0,SDS(),kMaxLevel)),
    tail_(nullptr),
    length_(0),
    level_(1),
    rng_(65536),
    coin_(0.25){}

SkipList::~SkipList(){
    // 沿层 0 迭代释放,不递归
    SkipListNode* node = header_->forward(0);
    while(node != nullptr){
        SkipListNode* next = node->forward(0);
        delete node;
        node = next;
    }
    delete header_;
}

int SkipList::getRandomLevel(){
    int level = 1;
    while(level < kMaxLevel && coin_(rng_)) ++level;
    return level;
}

void SkipList::insert(double score, SDS element){
    SkipListNode* current = header_;
    SkipListNode* update[kMaxLevel];
    size_t rank[kMaxLevel];     // rank[i] = update[i] 的名次

    for(int i = level_-1; i >= 0; --i){
        rank[i] = (i == level_-1) ? 0 : rank[i+1];      // 降层时 current 没动,名次从上层的算数接着用
        while(current->forward(i) != nullptr && precedes(*current->forward(i),score,element)){
            rank[i] += current->span(i);                // 先记这一步的跳幅,再走
            current = current->forward(i);
        }
        update[i] = current;
    }

    const int level = getRandomLevel();
    if(level > level_){
        for(int i = level_; i < level; ++i){
            rank[i] = 0;                    // header 的名次是 0
            update[i] = header_;
            update[i]->setSpan(i,length_);  // 预置"header 到表尾"的距离:拆边公式靠它算出 X→NULL 的剩余跨度
        }
        level_ = level;
    }

    SkipListNode* insertNode = new SkipListNode(score,std::move(element),level);
    for(int i = 0; i < level; ++i){
        insertNode->setForward(i,update[i]->forward(i));
        insertNode->setSpan(i,update[i]->span(i) - (rank[0] - rank[i]));   // 先读旧 span 算新节点
        update[i]->setForward(i,insertNode);
        update[i]->setSpan(i,rank[0] + 1 - rank[i]);                       // 再回写 update[i]
    }
    for(int i = level; i < level_; ++i){
        update[i]->setSpan(i,update[i]->span(i) + 1);   // 没够到的高层:跨度 +1
    }

    insertNode->setBackward(update[0] == header_ ? nullptr : update[0]);
    if(insertNode->forward(0) != nullptr){
        insertNode->forward(0)->setBackward(insertNode);
    }else{
        tail_ = insertNode;
    }
    ++length_;
}

int SkipList::Delete(double score,SDS element){
    SkipListNode* current = header_;
    SkipListNode* update[kMaxLevel];

    for(int i = level_-1; i>=0; --i){
        while(current->forward(i)&&precedes(*current->forward(i),score,element)){
            current = current->forward(i);
        }
        update[i] = current;
    }
    current = current->forward(0);
    if(current != nullptr
     && current->score() == score
     && current->element().compare(element) == 0){
      deleteNode(current,update);
      return 1;
    }
    return 0; 
}

void SkipList::deleteNode(SkipListNode* node,SkipListNode** update){
    for(int i=0;i<level_;i++){
        if(update[i]->forward(i) == node){
            update[i]->setForward(i,node->forward(i));
            update[i]->setSpan(i,update[i]->span(i)+node->span(i)-1);
        }else{
            update[i]->setSpan(i,update[i]->span(i)-1);
        }
    }
    //更新被删除节点的 前进/后退 指针
    if(node->forward(0)){
        node->forward(0)->setBackward(node->backward());
    }else{
        tail_ = node->backward();
    }

    while(level_>1 && header_->forward(level_-1)==nullptr){
        level_ --;
    }
    
    delete node;

    length_ --;
}

const SkipListNode* SkipList::find(double score, const SDS& element) const{
    SkipListNode* current = header_;

    for(int i = level_-1; i>=0; --i){
        while(current->forward(i) && precedes(*current->forward(i),score,element)){
            current = current->forward(i);
        }
    }

    current = current->forward(0);

    if(current!=nullptr 
        && current->score() == score
        && current->element().compare(element) == 0){
            return current;
    }
    return nullptr;
}

size_t  SkipList::rank(double score, const SDS& element) const{
    
    SkipListNode* current = header_;
    size_t rank = 0;

    for(int i = level_-1; i>=0; i--){
        while(current->forward(i) && precedes(*current->forward(i),score,element)){
            rank += current->span(i); 
            current = current->forward(i);
        }
    }
    current = current->forward(0);
    if(current!=nullptr
        && current->score() == score
        && current->element().compare(element) == 0){
            return rank + 1;
    }
    //未找到返回0
    return 0;
}

const SkipListNode* SkipList::nodeByRank(size_t rank) const{
    
    if( rank == 0 || rank>length_) return nullptr;

    SkipListNode* current = header_;
    for(int i = level_-1; i>=0; i--){
        while(current->forward(i) && current->span(i) <= rank){
            rank -= current->span(i);
            current = current->forward(i);
        }
        if(rank == 0) return current;
    }
    return nullptr;
}

bool SkipList::hasInRange(const ScoreRange& range) const{
    //排除空跳表
    if(tail_ == nullptr || header_->forward(0) == nullptr) return false;
    
    if(range.min > range.max ||
        (range.min == range.max && (range.maxExclusive !=false || range.minExclusive !=false))){
            return false;
    }

    if(!range.lteMax(header_->forward(0)->score())) return false;
    if(!range.gteMin(tail_->score())) return false;

    return true;
}

SkipListNode* SkipList::firstInRange(const ScoreRange& range) const{
    
    if(!hasInRange(range)) return nullptr;

    SkipListNode* current = header_;
    for(int i = level_-1; i>=0; i--){
        while(current->forward(i) && 
            !range.gteMin(current->forward(i)->score())){
                current = current->forward(i);
        }
    }
    current = current->forward(0);

    if(range.lteMax(current->score())) return current;

    return nullptr;
}

SkipListNode* SkipList::lastInRange(const ScoreRange& range) const{

    if(!hasInRange(range)) return nullptr;

    SkipListNode* current = header_;
    for(int i = level_-1; i>=0; i--){
        while(current->forward(i) && 
                range.lteMax(current->forward(i)->score())){
                    current = current->forward(i);
        }
    }

    if(range.gteMin(current->score())) return current;

    return nullptr;
}