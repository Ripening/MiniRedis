# MiniRedis

[![CI](https://github.com/Ripening/MiniRedis/actions/workflows/ci.yml/badge.svg)](https://github.com/Ripening/MiniRedis/actions/workflows/ci.yml)

用 C++17 从零实现的单线程 Redis:自己的 RESP2 协议解析器、跳表有序集合、AOF 持久化,
网络层基于自写的 myMuduo(epoll + 事件循环)。**不依赖 hiredis / redis-plus-plus / boost**。

## 特性

- **协议**:手写 RESP2 增量解析器,支持简单字符串 / 错误 / 整数 / 批量字符串 / 数组,
  半包自动等待、粘包连续解析
- **数据结构**:String、Hash、ZSet(跳表 + 字典双索引,支持 rank 与范围查询)
- **37 条命令**,大小写不敏感,覆盖 Connection / String / Key / Hash / ZSet 五类
- **过期键**:惰性删除 + 定时采样主动淘汰(类似 Redis 的 `activeExpireCycle`)
- **持久化**:AOF,支持 `always` / `everysec` / `no` 三种刷盘策略;
  前台重写与**后台重写(`bgrewriteaof`)**,重写期间新写入按缓冲增量合并
- **网络**:单线程反应堆,epoll 水平触发,优雅退出并 flush AOF

## 命令一览

| 分类 | 命令 |
|---|---|
| Connection | `PING` `ECHO` |
| String | `SET` `GET` `MSET` `MGET` `INCR` `INCRBY` `DECR` `DECRBY` |
| Key | `DEL` `EXISTS` `TYPE` `EXPIRE` `TTL` `PTTL` `PERSIST` |
| Hash | `HSET` `HGET` `HDEL` `HEXISTS` `HLEN` `HGETALL` |
| ZSet | `ZADD` `ZINCRBY` `ZSCORE` `ZCARD` `ZRANK` `ZREVRANK` `ZRANGE` `ZREVRANGE` `ZRANGEBYSCORE` `ZREVRANGEBYSCORE` `ZPOPMIN` `ZPOPMAX` |
| Persistence | `REWRITEAOF` `BGREWRITEAOF` |

## 架构

```
net/         myMuduo(epoll 事件循环)         收发字节流
protocol/    RESP 解析器                      字节流 → 命令
command/     命令分发 + 命令实现              SET/GET/HGET… → 调存储
storage/     InMemoryDB                       按 key 查对象 + 过期管理
object/      RedisObject + ZSet               类型从这里开始存在
core/        SDS / DICT<V> / SkipList         通用原语,零 Redis 知识
persistence/ AOF                              命令追加 + 重写 + 回放
```

分层判据一句话:**能不能脱离 Redis 理解。** 能 → `core`;不能 → `object` 及以上。
跳表与 Redis 源码(`src/t_zset.c`)的逐接口对照表在
[docs/zset-reading-map.md](docs/zset-reading-map.md),其余设计决策见
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 构建与运行

需要 CMake ≥ 3.16 和支持 C++17 的编译器。服务端(net 层)只在 Linux 下编译;
macOS / Windows 上仍可编译 `miniredis_core` 与单元测试。

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

./build/miniredis_server --port 6379 --dir /var/lib/miniredis
```

`--dir` 是 AOF 的落盘目录(启动时 `chdir` 过去)。用任意 Redis 客户端连接:

```bash
redis-cli -p 6379 set foo bar
redis-cli -p 6379 zadd rank 100 alice 90 bob
redis-cli -p 6379 zrange rank 0 -1 withscores
```

## 测试

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMINIREDIS_BUILD_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

8 个测试二进制、147 个用例,按层分开:sds / dict / skiplist / object / zset /
storage / resp / command。首次配置会通过 FetchContent 拉取 googletest,需要能访问 GitHub。

## 压测

`redis-benchmark`(Redis 6.0.16)在 3 核容器里自压。服务器为**单线程 + AOF
(`appendfsync everysec`)**,构建 `-O3 -DNDEBUG`;每个测点重启服务、预置 10 万键,
重复 3 次取中位。复现脚本见 [scripts/bench.sh](scripts/bench.sh)。

### 单命令吞吐

`-c 50 -n 200000 -r 100000`

| 命令 | QPS | p50 (ms) | p99 (ms) |
|---|---:|---:|---:|
| ping_mbulk | 294,551 | 0.1 | 0.2 |
| set | 246,609 | 0.2 | 0.4 |
| get | 289,436 | 0.1 | 0.2 |
| incr | 238,949 | 0.2 | 0.4 |
| hset | 242,131 | 0.2 | 0.4 |
| mset | 111,919 | 0.5 | 0.9 |
| zadd | 182,149 | 0.3 | 0.6 |
| zpopmin | 255,754 | 0.2 | 0.4 |

- `mset` 一条请求写 **10 对键值**(redis-benchmark 的固定模板),单位工作量是 `set`
  的 10 倍,所以 QPS 低不代表实现慢。
- `zpopmin` 压的是**空 zset**:模板是 `ZPOPMIN myzset`(不带 `__rand_int__`,且只弹不压),
  数字反映的是空转路径,不能当作跳表删除性能看。
- `zadd` 走跳表插入,比哈希表写入略慢。

### 并发梯度

`-n 200000 -r 100000`

| 并发 | SET QPS | SET p99 | GET QPS | GET p99 |
|---:|---:|---:|---:|---:|
| 1 | 36,023 | 0.1 | 39,872 | 0.1 |
| 10 | 234,192 | 0.1 | 274,725 | 0.1 |
| 50 | 242,424 | 0.4 | 290,698 | 0.2 |
| 100 | 250,627 | 0.8 | 298,063 | 0.3 |
| 200 | 259,067 | 1.5 | 307,220 | 0.7 |

单连接 ~3.6 万 QPS 由往返延迟主导;并发 10 就已把单线程打满,之后基本持平。
p99 随并发单调上升(排队效应),但绝对值仍在 2 ms 以内。

### 管道

`-n 200000 -c 50 -r 100000`

| 管道深度 | SET QPS | GET QPS |
|---:|---:|---:|
| 1 | 243,013 | 289,436 |
| 16 | 543,478 | 2,173,913 |
| 64 | 696,864 | 3,076,923 |

管道摊掉了 syscall 与事件循环的固定开销,GET 提升约 10 倍。
SET 涨幅小得多,瓶颈转移到 AOF 的写盘路径。

### 值大小

`-c 50 -r 100000`

| 值大小 | 请求数 | SET QPS | GET QPS |
|---:|---:|---:|---:|
| 3 B | 200,000 | 244,200 | 289,017 |
| 64 B | 200,000 | 232,829 | 289,855 |
| 1 KB | 200,000 | 186,916 | 288,600 |
| 4 KB | 50,000 | 168,919 | 297,619 |

读路径几乎不受值大小影响(一次拷贝);写路径随值增大下滑,
因为整条命令要按 RESP 再序列化一份进 AOF。

### 稳定性与资源

持续写入 15 s、其间触发 **6 次 AOF 重写**:

| 指标 | 值 |
|---|---|
| 进程 | 存活,退出时正常 flush |
| 残留 `.tmp.bg` 临时文件 | 0 |
| fd 数(压测前 / 后) | 7 / 7 |
| RSS(压测前 / 后) | 3.4 MB / 60 MB |
| 服务器 stderr | 空 |

11 MB 的 AOF 重启回放耗时 **0.14 s**(三次一致,约 80 MB/s)。

## 已知限制

- 只实现 RESP2,且**不支持内联命令**(inline protocol):收到非 `*` 开头的输入会回协议错误并
  关闭连接,所以 `redis-benchmark` 的 `PING_INLINE` 用不了
- 未实现 `CONFIG`,客户端连接后的例行探测会拿到 `ERR unknown command 'CONFIG'`
- 数据类型只有 String / Hash / ZSet,没有 List / Set
- `SET` 不支持 `EX` 等选项;`EXPIRE` 写进 AOF 的是相对秒数,回放时按回放时刻重新计时,
  与真 Redis 存绝对时间戳的做法不同
- 单线程,无主从 / 集群 / 哨兵
- 持久化只有 AOF,没有 RDB 快照
