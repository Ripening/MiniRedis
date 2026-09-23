# MiniRedis 架构定稿

> 本文是**设计决策的权威存档**,代码改动前先读这里。

## 一、分层结构

```
┌─────────────────────────────────────────────┐
│  net/        myMuduo(epoll 事件循环)          │  收发字节流
├─────────────────────────────────────────────┤
│  protocol/   RESP 解析器                      │  字节流 → 命令
├─────────────────────────────────────────────┤
│  command/    命令分发 + 命令实现              │  SET/GET/HGET… → 调存储
├─────────────────────────────────────────────┤
│  storage/    InMemoryDB                       │  按 key 查对象 + 过期管理
├─────────────────────────────────────────────┤
│  object/     RedisObject + ZSet               │  类型从这里开始存在
├─────────────────────────────────────────────┤
│  core/       SDS / DICT<V> / SkipList         │  通用原语,零 Redis 知识
├─────────────────────────────────────────────┤
│  persistence/ AOF                             │  命令追加 + 重写 + 回放
└─────────────────────────────────────────────┘
```

(`persistence/` 画在最下层是为了排版整齐;它实际是被 `command/` 调用的,不参与内存态。) 

**分层判据(一句话):能不能脱离 Redis 理解。** 能 → core;不能 → object 及以上。

| 层 | 内容 | 状态 |
|---|---|---|
| core | `SDS`(动态字符串)、`DICT<V>`(哈希表)、`SkipList`(跳表) | ✅ 完成 |
| object | `redisObject`(类型体系 + RAII 所有权)、`ZSet`(双索引) | ✅ 完成 |
| storage | `InMemoryDB`(含过期管理、快照导出) | ✅ 完成 |
| protocol | RESP2 增量解析器(半包/粘包/二进制安全) | ✅ 完成 |
| command | 37 条命令 + 表驱动分发 + 重写触发器 | ✅ 完成 |
| net | myMuduo 接入(单线程 Reactor,优雅退出) | ✅ 完成 |
| persistence | AOF(三档 fsync、前后台重写、回放) | ✅ 完成 |

测试 8 个二进制 / 147 用例,CI 在 Debug 与 Release 两档下各跑一遍。

## 二、对象层设计(已定稿)

### 2.1 RedisObject 形态

```cpp
class redisObject final {
    enum class RedisObjectType     { STRING = 0, HASH = 1, ZSET = 2 };
    enum class RedisObjectEncoding { RAW = 0, HASHTABLE = 1, SKIPLIST = 2 };
    RedisObjectType     type_;
    RedisObjectEncoding encoding_;
    void*               ptr_;      // 指向载荷(SDS* / DICT<SDS>* / ZSet*)
};
```

- **具名工厂 + 私有构造**:`createStringObject` / `createHashObject` / `createZSetObject` 返回 `unique_ptr`
- **拷贝与移动均禁用**:根因是裸指针 `void* ptr_`;移动虽可正确实现,但无使用场景(对象永远由 unique_ptr 原地持有)
- **析构 switch 分派**:`delete static_cast<DICT<SDS>*>(ptr_)`——模板化后不再需要遍历 delete 值
- **注意**:`freeRedisObject` 的 switch 没有 default,漏写一个 case 编译器不会警告(曾漏过 `ZSET`),新增类型时必查

### 2.2 值层:键值统一用 SDS(不用 std::string)

三个层次各自的语言:**存储层 SDS / 接口层 string_view / 工具代码 std::string**。

```
用完就扔 std::string,借来借去 string_view,要存下来 SDS
```

**面试答法「为什么不用 std::string」**(姿态:先承认 std::string 好,再讲差异):
1. **能力差异**(非偏好):embstr 要求对象头与字符串同一块内存——std::string 的 buffer 永远自己管理,物理上做不到
2. **每对象内存核算**:`MEMORY USAGE` 要 per-object 归因,SDS 头部 alloc 字段 O(1) 读出真实分配字节;std::string 给不出(SSO 零分配、capacity 语义实现定义)
3. (次要)大值 append 增长策略可控(<1MB 翻倍、≥1MB +1MB),std::string 增长因子实现定义

## 三、DICT 模板化(已定稿)

**单参数 `DICT<V>`,SDS key 硬编码。**

| 使用点 | 实例 | 为什么 |
|---|---|---|
| 全局数据库 | `DICT<unique_ptr<redisObject>>` | 顶层要多态,RedisObject 统一收容 |
| Hash 对象内层 | `DICT<SDS>` | field→value 字符串映射,类型确定 |
| ZSet 内层 | `DICT<double>` | member→score,双索引里的字典那一半(见 §4) |

**为什么模板化**(设计决策素材):
- 参考实现用 `void*` 存储值 → **类型信息在容器层丢失** → 析构依赖运行时 switch 分派,且与 int 编码的假指针释放冲突
- 模板化后**所有权随类型进入编译期**,Hash 析构不再需要 `forEach(delete)`
- 收益具体化:ZSet 内层 `DICT<double>` 可直接 `dict.set(member, score)`,零 cast、零手动释放

**边界(明确不做)**:
- 不做 `DICT<K,V>`——两个用例 key 都是 SDS
- **不消灭全部运行时类型信息**:命令层查 type 再转(WRONGTYPE 判断)是**必然**的,模板只管容器存储与释放
- 三个 struct(`dictEntry`/`dictht`/`dictStruct`)移入类内是 **V 依赖链带动**的:V 进 dictEntry → dictht 持有 dictEntry** → dict 持有 dictht

**形态约束**:模板实现必须全在 hpp(调用点需看到完整定义才能实例化),原 dict.cpp 已删除。

## 四、未来路线上的定论(避免重复讨论)

- **跳表归宿 = ZSet**,不做 String/Hash 的主索引
- **ZSet ≠ 跳表**:双索引 = 跳表(有序/范围/rank)+ `DICT<double>`(member→score 精确查, O(1))。只放跳表会让 ZSCORE/判重变 O(n)。字典存的是**分数副本**而不是跳表节点指针:`DICT<SkipListNode*>` 要改 core 的 insert 返回值与 const 出口,省不下多少内存,反而让删除顺序变得敏感
- **跳表放 core**(通用有序容器),Redis 语义包装在 object 层的 ZSet 类
- **编码体系**:落地 String-RAW、Hash-HASHTABLE、ZSet-SKIPLIST;int / embstr / listpack 设计预留。embstr 需对象头与 SDS 共享分配,为不污染 core 层 SDS 通用性而不做
- **AOF 纳入 v1**(已落地):回放 / 前后台重写 / 三种 fsync 策略
- **范围上限**:单线程 + String/Hash/ZSet + TTL + AOF + 测试三件套;不碰多线程/事务/集群/List/Set

## 五、写单机时的三条纪律(为将来上分布式留路)

1. **写路径唯一**——所有状态变更都经过命令日志,不留绕过日志改内存的口子
2. **确定性**——命令不依赖本地时钟/随机数做决策;TTL 存绝对时间戳由主生成随命令复制
3. **状态机与网络解耦**——InMemoryDB 不碰 socket

## 六、参考实现对照

| | 参考项目 TinyRedis | 本项目 |
|---|---|---|
| 网络层 | 裸 epoll | **myMuduo**(自研 Reactor) |
| ZSet | 未实现 | **跳表 + 字典双索引**(其 roadmap 未做项) |
| 对象层 | C 式简化(new std::string) | **RAII + SDS 键值统一** |
| 容器 | `void*` 值 | **模板化 DICT<V>** |
| 测试 | GTest + CI | 同款,CI 跑 Debug / Release 双档 |
