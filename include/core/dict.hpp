#pragma once
#include "sds.hpp"
#include <functional>
#include <string_view>
template <typename V>
class DICT
{
private:
    struct dictEntry{
        SDS key;
        V value;
        dictEntry* next;
    };
    struct dictht{
        dictEntry** table;
        size_t size;
        size_t sizemask;
        size_t used;
    };
    struct dictStruct{
        dictht ht[2];
        long rehashidx;
    };
public:
    DICT();
    ~DICT();
    DICT(DICT&& other) noexcept;
    DICT& operator= (DICT&& other) noexcept;

    bool set(SDS&& key,V value);
    const V* get(const SDS&key) const;
    bool erase(const SDS&key);
    size_t size() const;
    void forEach(const std::function<void(const SDS&, const V&)>& fn) const;
private: 
    //  初始化/释放哈希表
    void initTable(dictht& ht,size_t size);
    void freeTable(dictht& ht);

    //对哈希表的增删改查
    void insertEntryToTable(dictht& ht, dictEntry* entry, size_t hash);
    bool eraseFromTable(dictht& ht, const SDS& key);
    dictEntry* findEntryInTable(const dictht& ht, const SDS& key) const;
    //渐进式reHash
    bool isRehashing() const;
    void finishRehash();
    void rehashStep();

    void expandIfNeeded();
    void clear();
private:
    static size_t hashkey(const SDS& key) {
        return std::hash<std::string_view>{}(std::string_view(key.c_str(), key.len()));
    }
    static bool keyEquals(const SDS& lhs, const SDS& rhs){
        return lhs.compare(rhs) == 0;
    }
    static size_t roundToPowerOfTwo(size_t size){
        size_t out = 4;
        while(out<size){
            out<<=1;
        }
        return out;
    }
private:
    dictStruct dict_;
};


template <typename V>
bool DICT<V>::isRehashing() const{
    return dict_.rehashidx != -1 ? true:false;
}

// 初始化/释放表
template <typename V>
void DICT<V>::initTable(dictht& ht,size_t size){
    size = roundToPowerOfTwo(size);
    ht.table = new dictEntry*[size]();
    ht.size = size;
    ht.sizemask = size-1;
    ht.used = 0;
}
template <typename V>
void DICT<V>::freeTable(dictht& ht){
    if(ht.table == nullptr) return;
    for(size_t i = 0;i<ht.size;i++){
        dictEntry* entry = ht.table[i];
        while(entry!=nullptr){
            dictEntry* next = entry->next;
            delete entry;
            entry = next;
        }
    }
    delete[] ht.table;
    ht.table = nullptr;
    ht.size = ht.sizemask = ht.used = 0;
}
template <typename V>
void DICT<V>::clear(){
    freeTable(dict_.ht[0]);
    freeTable(dict_.ht[1]);
    dict_.rehashidx = -1;
}
template <typename V>
DICT<V>::DICT(){
    dict_.rehashidx = -1;
    initTable(dict_.ht[0],8);
    dict_.ht[1].table = nullptr;
    dict_.ht[1].size = dict_.ht[1].sizemask = dict_.ht[1].used = 0;
}
template <typename V>
DICT<V>::~DICT(){
    clear();
}
template <typename V>
DICT<V>::DICT(DICT&& other) noexcept{
    dict_ = other.dict_;
    other.dict_.ht[0].table = nullptr;
    other.dict_.ht[1].table = nullptr;

    other.dict_.ht[0].size = other.dict_.ht[0].sizemask = other.dict_.ht[0].used = 0;
    other.dict_.ht[1].size = other.dict_.ht[1].sizemask = other.dict_.ht[1].used = 0;

    other.dict_.rehashidx = -1;
}
template <typename V>
DICT<V>& DICT<V>::operator= (DICT&& other) noexcept{
    if(this!=&other){
        clear();
        dict_ = other.dict_;
        other.dict_.ht[0].table = nullptr;
        other.dict_.ht[1].table = nullptr;

        other.dict_.ht[0].size = other.dict_.ht[0].sizemask = other.dict_.ht[0].used = 0;
        other.dict_.ht[1].size = other.dict_.ht[1].sizemask = other.dict_.ht[1].used = 0;

        other.dict_.rehashidx = -1;
    }
    return *this;
}
template <typename V>
void DICT<V>::rehashStep(){
    if(!isRehashing()) return;
    if(dict_.ht[0].used == 0){
        finishRehash();
        return;
    }
    while(dict_.rehashidx < static_cast<long>(dict_.ht[0].size)&&
        dict_.ht[0].table[dict_.rehashidx]==nullptr){
            dict_.rehashidx++;
    }
    if(dict_.rehashidx >= static_cast<long>(dict_.ht[0].size)){
        finishRehash();
        return;
    }
    dictEntry* entry = dict_.ht[0].table[dict_.rehashidx];
    dict_.ht[0].table[dict_.rehashidx] = nullptr;
    while(entry!=nullptr){
        dictEntry* next = entry->next;
        const size_t hash = hashkey(entry->key);
        insertEntryToTable(dict_.ht[1],entry,hash);
        dict_.ht[0].used --;
        entry = next;
    }
    dict_.rehashidx++;

    if(dict_.ht[0].used==0)
        finishRehash();
}
template <typename V>
void DICT<V>::finishRehash(){
    delete[] dict_.ht[0].table;
    dict_.ht[0] = dict_.ht[1];
    dict_.ht[1].table = nullptr;
    dict_.ht[1].size = dict_.ht[1].sizemask = dict_.ht[1].used = 0;
    dict_.rehashidx = -1;
}
template <typename V>
void DICT<V>::expandIfNeeded(){
    // 渐进式rehash过程中不能扩容
    if(isRehashing()) return;

    if(dict_.ht[0].used>=dict_.ht[0].size){
        initTable(dict_.ht[1],2*dict_.ht[0].size);
        dict_.rehashidx = 0;
    }
}
template <typename V>
bool DICT<V>::set(SDS&& key,V value){
    if(isRehashing()){
        rehashStep();
    }
    expandIfNeeded();
    //尝试更新
    if(dictEntry* entry = findEntryInTable(dict_.ht[0],key)){
        entry->value = std::move(value);
        return true;
    }
    if(isRehashing()){
        if(dictEntry* entry = findEntryInTable(dict_.ht[1],key)){
            entry->value = std::move(value);
            return true;
        }
    }
    //插入数据
    const size_t hash = hashkey(key);
    dictEntry* entry = new dictEntry();
    entry->key = std::move(key);
    entry->value = std::move(value);

    dictht& target = isRehashing()?dict_.ht[1]:dict_.ht[0];
    insertEntryToTable(target,entry,hash);
    return true;
}
template <typename V>
const V* DICT<V>::get(const SDS&key) const{
    
    if(isRehashing()){
        // rehash 只搬内部桶,不改变"字典里有哪些键值"这个逻辑状态,
        // 所以 const 读操作也允许推进它
        const_cast<DICT*>(this)->rehashStep();
    }
    if(dictEntry* entry = findEntryInTable(dict_.ht[0],key)){
        return &entry->value;
    }
    if(isRehashing()){
       if(dictEntry* entry = findEntryInTable(dict_.ht[1],key)){
            return &entry->value;
        }
    }
    return nullptr;
}
template <typename V>
bool DICT<V>::erase(const SDS&key){
    if(isRehashing()) rehashStep();

    if(eraseFromTable(dict_.ht[0],key)){
        if(isRehashing()&&dict_.ht[0].used == 0) finishRehash();

        return true;
    }
    if(isRehashing()&&eraseFromTable(dict_.ht[1],key)){
        return true;
    }
    return false;
}
template <typename V>
size_t DICT<V>::size() const{
    return dict_.ht[0].used + dict_.ht[1].used;
}

template <typename V>
void DICT<V>::forEach(const std::function<void(const SDS&, const V&)>& fn) const {
    if (!fn) {
        return;
    }

    for (int t = 0; t < 2; ++t) {
        const dictht& ht = dict_.ht[t];
        if (ht.table == nullptr) {
            continue;
        }
        for (size_t i = 0; i < ht.size; ++i) {
            dictEntry* entry = ht.table[i];
            while (entry != nullptr) {
                fn(entry->key, entry->value);
                entry = entry->next;
            }
        }
    }
}
//头插
template <typename V>
void  DICT<V>::insertEntryToTable(dictht& ht, dictEntry* entry, size_t hash){
    const size_t index = hash & ht.sizemask;
    entry->next = ht.table[index];
    ht.table[index] = entry;
    ht.used ++ ;
}
template <typename V>
bool DICT<V>::eraseFromTable(dictht& ht, const SDS& key){
    if (ht.table == nullptr || ht.size == 0) {
        return false;
    }
    const size_t index = hashkey(key) & ht.sizemask;
    dictEntry* entry = ht.table[index];
    dictEntry* prev = nullptr;

    while (entry != nullptr) {
        if (keyEquals(entry->key, key)) {
            if (prev != nullptr) {
                prev->next = entry->next;
            } else {
                ht.table[index] = entry->next;
            }
            delete entry;
            ht.used--;
            return true;
        }

        prev = entry;
        entry = entry->next;
    }

    return false;
}
template <typename V>
typename DICT<V>::dictEntry* DICT<V>::findEntryInTable(const dictht& ht, const SDS& key) const{
    if(ht.table == nullptr || ht.used == 0) return nullptr;
    const size_t index = hashkey(key) & ht.sizemask;
    dictEntry* entry = ht.table[index];
    while(entry!=nullptr){
        if(keyEquals(entry->key, key)){
            return entry;
        }
        entry = entry->next;
    }
    return nullptr;
}




