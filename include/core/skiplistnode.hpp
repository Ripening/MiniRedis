#pragma once
#include "core/sds.hpp"
#include <vector>

class SkipListNode
{
public:
    SkipListNode(double score,SDS element,int height)
        :score_(score),element_(std::move(element)),backward_(nullptr),forward_(height){}
    ~SkipListNode() = default;

    double score() const {return score_;}
    const SDS& element() const { return element_;}
    
    SkipListNode* forward(int i) const { return forward_[i].next;}
    void setForward(int i, SkipListNode* node) { forward_[i].next = node; }
    size_t span(int i) const { return forward_[i].span;}
    void setSpan(int i,size_t span){ forward_[i].span = span; }
    
    SkipListNode* backward() const { return backward_;}
    void setBackward(SkipListNode* node){ backward_ = node; }
    
private:
    struct Level{
        SkipListNode* next = nullptr;
        size_t span = 0;
    };
private:
    double              score_;
    SDS                 element_;
    SkipListNode*       backward_;
    std::vector<Level>  forward_;
};




