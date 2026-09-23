# 跳表 / ZSet 源码阅读对照表

> 对照对象:redis-3.0-annotated(黄健宏中文注释版),本地:`~/references/redis-3.0-annotated`
> 跳表 + ZSet 代码几乎全在 `src/t_zset.c`(3.0 无独立 zskiplist.c),唯一例外:`zrangespec` 在 `src/redis.h:1705-1716`
> 状态列截至 2026-09-23(跳表层与 ZSet 层均已写完并过测试)

## 跳表接口对照

| MiniRedis 接口 | 状态 | Redis 对应 | 位置 (t_zset.c) |
|---|---|---|---|
| `SkipList()` | ✓ 已写 | `zslCreate` + `zslCreateNode` | :103 + :86 |
| `~SkipList()` | ✓ 已写 | `zslFree` + `zslFreeNode` | :146 + :134 |
| `getRandomLevel()` | ✓ 已写 | `zslRandomLevel` | :181 |
| `insert(score, ele)` | ✓ 已写 | `zslInsert` | :198 |
| `Delete(score, ele)` | ✓ 已写 | `zslDelete` + `zslDeleteNode` | :352 + :316 |
| `find(score, ele)` | ✓ 已写 | 无独立函数(查找骨架内嵌在 Delete 里) | 参考 :352 前半段 |
| `rank(score, ele)` | ✓ 已写 | `zslGetRank` | :691 |
| `nodeByRank(r)` | ✓ 已写 | `zslGetElementByRank` | :735 |
| `hasInRange(range)` | ✓ 已写 | `zslIsInRange` | :424 |
| `firstInRange(range)` | ✓ 已写 | `zslFirstInRange` | :455 |
| `lastInRange(range)` | ✓ 已写 | `zslLastInRange` | :492 |
| `size()` | ✓ 字段直读 | `zsetLength`(zset 层包装) | :1575 |
| `first()` | 平凡 | 无独立函数(即 `header->level[0].forward`) | — |

## 区间组辅助

| MiniRedis | Redis 对应 | 位置 |
|---|---|---|
| `ScoreRange{min,max,minExclusive,maxExclusive}` | `zrangespec{min,max,minex,maxex}` | redis.h:1705-1716 |
| 边界判断(内部函数或直接内联) | `zslValueGteMin` / `zslValueLteMax` | t_zset.c :402 / :413 |

`zslIsInRange` 只是这两者的组合检查(首节点 ≥ min 且 尾节点 ≤ max),比想象的短。

## 暂不做(ZREMRANGEBY* 才需要,命令清单里没有)

`zslDeleteRangeByScore:541` / `zslDeleteRangeByLex:578` / `zslDeleteRangeByRank:629`

## zset 层

| 内容 | 位置 |
|---|---|
| ZADD 总逻辑(含"更新分数 = 删了重插",3.0 无独立 zsetAdd 函数) | `zaddGenericCommand:1718` |
| 编码转换(ziplist ↔ skiplist,本项目只有跳表一种编码,可跳过) | `zsetConvert:1595` |

## 读源码注意

1. **骨架三件套**:层循环下降 / `rank += span` 沿路累加 / 停下后判等——insert、delete、zslGetRank 共用同一套,读第三个时最省力。
2. **判等细节**:`zslGetRank` 只用 `equalStringObjects` 比元素不比 score(zset 元素唯一);MiniRedis 的 Delete 连 score 一起比,是裸跳表层的防御。
3. **C vs C++**:`robj*` 带引用计数(`decrRefCount`),MiniRedis 用 SDS 值语义——所有 refcount 相关行直接跳过。
4. **比较函数**:`compareStringObjects` ↔ `SDS::compare`,语义相同。
5. **随机层**:`zslRandomLevel` 循环内不卡上限、末尾截断;MiniRedis 循环内卡上限——行为等价,都返回 1..32。
