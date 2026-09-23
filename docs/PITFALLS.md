# 踩坑记录

> 记录已经踩过的坑 + **已知的未来雷区**(写代码前扫一眼,别重复掉).

## 一、模板相关(实战踩过)

### 1. 类外定义返回嵌套类型必须加 typename

```cpp
// ❌ 编译器不认识 dictEntry —— 类作用域还没进入
DICT<V>::dictEntry* DICT<V>::findEntryInTable(...)

// ✅ 返回类型位置必须写 typename
typename DICT<V>::dictEntry* DICT<V>::findEntryInTable(...)
```

**规则**:类外定义的成员函数,若返回**类内嵌套类型**,返回类型位置要加 `typename`(参数位置不用)。
**报错关键词**:`dependent name` / `missing 'typename' prior to dependent type name`

### 2. 构造函数/析构函数的类外定义**不带**返回类型

```cpp
DICT<V>::DICT() { }        // ✅ 构造函数就是这样
DICT<V>::~DICT() { }       // ✅ 析构同理
bool DICT<V>::set(...) { } // ✅ 普通函数才有返回类型
```

构造函数没有返回值,`::` 前的 `DICT<V>` 只是限定名的一部分。

### 3. 模板实例化是"懒"的

**编译通过 ≠ 代码被验证过。** `DICT<SDS>` 在被实际使用前,编译器不会生成代码,写错也无声无息。
本次教训:`createHashObject()` 里的 `DICT<SDS>` 在写了测试之后才第一次被真正编译。

**对策**:每写完一层,立刻写调用它的测试。

### 4. 模板实现必须全在头文件

`.cpp` 里的模板定义,调用点看不见 → 链接期 `undefined symbol`。
本项目 dict.cpp 已删除,实现全在 `include/core/dict.hpp`。

---

## 二、对象层(常驻雷区)

### 5. `void* ptr_` 的两笔债

**债一:析构必须 switch 分派**——类型信息在 `void*` 里丢了,只能靠 `type_` 手工还原。
**债二:拷贝/移动必须手动禁用**——裸指针默认浅拷贝,两份对象指向同一块内存 → double free。
```cpp
redisObject(const redisObject&) = delete;   // 不是选择,是唯一自洽的答案
```

### 6. ⚠️ int 编码陷阱(未来踩)

等实现 String 的 int 编码后,`ptr_` 里存的是**整数本身**(假指针):

```cpp
case RedisObjectType::STRING:
    // 必须判编码!否则对假指针 delete → 立刻崩
    if (encoding_ == RedisObjectEncoding::RAW) delete static_cast<SDS*>(ptr_);
    break;
```

**同理 Hash 以后加 listpack 编码时**,析构也要先判 encoding 再选释放方式。

### 7. 工厂的异常安全(已记录未改)

```cpp
new redisObject(..., new SDS(str));
//   ↑ 若这里抛异常  ↑ 这块内存泄漏
```
修法:先用 unique_ptr 接管载荷,再 `release()` 给构造。

### 7.1 ⚠️ `freeRedisObject` 的 switch 没有 default(踩过)

新增类型时漏写一个 `case`,**编译器一声不吭**,析构函数静默跳过 → 该类型的对象全部泄漏。
加 ZSet 时就漏过一次 `case ZSET`(ASan 查不出来,靠 MAC 上的 `leaks --atExit` 才发现)。
**对策**:`redisObject` 新增任何一种类型,第一件事是去 `freeRedisObject` 补 case;
析构里加不变量断言是更硬的办法。

---

## 三、SDS / core 层

### 8. SDS 是 move-only

`SDS(const SDS&) = delete` —— 所以 `DICT<SDS>` 里 `set` 的 `std::move` 是**必需**的,不写连实例化都过不了。
写 `DICT<别的类型>` 时先确认:这个类型可拷贝还是只能移动?

### 9. 谁分配谁释放

- SDS 是 **malloc 户**(`free` 配对)
- DICT / dictEntry 是 **new 户**(`delete` 配对)
混用即崩。

---

## 四、构建 / 环境

### 10. `target_include_directories` 必须在 `add_library` 之后

顺序反了 cmake 直接报 target 不存在。

### 11. gtest 下载失败

FetchContent 首次 configure 要访问 GitHub,失败是网络问题不是配置问题。清缓存重试:
```bash
find build -mindepth 1 -delete
cmake -S . -B build -DMINIREDIS_BUILD_TESTS=ON
```

### 12. 构建命令(含测试)

```bash
cmake -S . -B build -DMINIREDIS_BUILD_TESTS=ON
cmake --build build -j8
cd build && ctest --output-on-failure
```

---

## 五、storage 层(InMemoryDB)

### 13. 移植"取指针→判空→释放"三段式时逻辑写反(已修)

参考实现 `eraseExpire` 是三步:`get → 判 null → erase → delete 指针`。值语义下次改写时把 null 检查写反了:

```cpp
// ❌ 有 TTL 的 key 第一行就 return false,erase 根本没执行
if(auto expireTime = expires_.get(SDS(key))){ return false; }
if(expires_.erase(SDS(key))) return false;
return true;

// ✅ 值语义下塌缩成一行(没有指针要释放)
return expires_.erase(SDS(key));
```

**后果链**:SET 清 TTL 失效 → 过期 key 被访问后 expires_ 残留旧时间戳 → 重新 SET 的新值一进来就被判"已过期",下次访问立刻被删。
**教训**:改写类函数(erase/delete 语义)后,拿一条真实数据 trace 一遍再提交。

### 14. `&&` 串联两个 erase 当返回值(已修)

```cpp
// ❌ 无 TTL 的 key:左边删成功(true)、右边没东西删(false)→ 整体 false,误报失败
return kv_.erase(SDS(key)) && expires_.erase(SDS(key));

// ✅ 主删除决定返回值,过期表是"顺带清理",返回值可忽略
bool erased = kv_.erase(SDS(key));
expires_.erase(SDS(key));
return erased;
```

**记住**:`erase` 返回的是"删没删掉",不是"操作成不成功"。参考实现里 `(void)eraseExpire(key)` 的 void 强转就是在示意"这个返回值不要参与逻辑"。

---

## 六、只在优化档下暴露的坑

### 15. ⚠️ `open()` 带 `O_CREAT` 却没给 mode:O0 静默、O1+ 直接 abort

```cpp
// ❌ 三参数版 open 少写 mode;O_CREAT 时 mode 是必需的
int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND);

// ✅
int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
```

`_FORTIFY_SOURCE` 会在编译期认出这个模式并插检查,**但它只在 `-O1` 及以上生效**。
于是同一个文件:容器里 `-O0` 编出来能跑,一开 `-O3` 跑压测立刻
`*** invalid open call: O_CREAT or O_TMPFILE without mode ***` 然后被 abort 掉。

**这个 bug 潜伏了很久没暴露**,因为 CMakeLists 里没有默认 `CMAKE_BUILD_TYPE`——
所有本地构建都是 -O0。**对策(已落地)**:CI 用 Debug/Release 矩阵,两个优化档都编一遍、
都跑测试;本地压测脚本固定走 Release。

### 16. `std::string::erase(0, n)` 逐条调用 = O(n²)

从缓冲区头部消费数据的常见写法:

```cpp
// ❌ 每解析一条命令就搬一次,剩余部分整体 memmove
if (pos_ > 0) { buffer_.erase(0, pos_); pos_ = 0; }
```

单条命令看不出问题;一旦把整份 AOF(几十 MB)一次性 `feed()` 进来,复杂度就退化了。
实测每翻倍耗时约 4 倍:25k 命令 0.31 s → 100k 3.66 s → 200k 13.9 s。

```cpp
// ✅ 消费过半才搬移,摊还 O(1)/字节
if (pos_ > 0 && pos_ > buffer_.size() / 2) { buffer_.erase(0, pos_); pos_ = 0; }
```

改完实测 0.10 s 上下持平(与命令数无关),27 MB 的 AOF 回放从外推的 ~140 s 降到 0.32 s。

**通用判据**:凡是"从容器头部删元素"的操作,先问一句"这个容器会不会很大、删会不会很频繁"——
`vector`/`string` 头部删除是整体搬移,`deque`/环形缓冲才便宜。
