#!/usr/bin/env bash
# MiniRedis 压测。必须在 Linux 容器里跑(要 redis-benchmark 和服务器的 epoll 侧):
#   docker exec kv-dev bash /miniredis/scripts/bench.sh
#
# 可调环境变量:
#   PORT      服务器端口,默认 6399
#   REPS      每个测点重复次数(取中位),默认 3
#   N_MAIN    吞吐矩阵的请求数,默认 200000
#   KEYSPACE  随机键空间(-r),默认 100000
#   SKIP_BUILD=1  跳过编译
#
# 每个测点前重启服务器并清空数据目录,保证各次测量从同一初始状态出发。
set -u

PORT="${PORT:-6399}"
REPO="${REPO:-/miniredis}"
BUILD="${BUILD:-/tmp/bench-build}"
RUN="${RUN:-/tmp/bench-run}"
REPS="${REPS:-3}"
N_MAIN="${N_MAIN:-200000}"
KEYSPACE="${KEYSPACE:-100000}"

# ---------------- 服务器生命周期 ----------------

# 只按精确 argv[1] 匹配:二进制名超过 15 字符,comm 被截断,pkill -x/pgrep -f 都会误伤
srv_pids(){ ps -eo pid,args | awk -v b="$BUILD/miniredis_server" '$2==b{print $1}'; }

stop_srv(){
    local p
    for p in $(srv_pids); do kill "$p" 2>/dev/null; done
    sleep 0.3
}

start_srv(){
    stop_srv
    rm -rf "$RUN"; mkdir -p "$RUN"
    nohup "$BUILD/miniredis_server" --port "$PORT" --dir "$RUN" >"$RUN/srv.log" 2>&1 &
    local i
    for i in $(seq 1 50); do
        redis-cli -p "$PORT" ping >/dev/null 2>&1 && return 0
        sleep 0.1
    done
    echo "!! 服务器起不来,$RUN/srv.log:" >&2
    cat "$RUN/srv.log" >&2
    return 1
}

# ---------------- 结果提取 ----------------

# redis-benchmark 用 \r 刷新同一行,先统一成 \n 再抓
rps(){ tr '\r' '\n' <"$1" | awk '/requests per second/{for(i=1;i<=NF;i++) if($i=="requests"){print $(i-1); exit}}'; }

# 这个版本的 redis-benchmark 不打延迟分位表,只打累积分布:
#     77.75% <= 0.1 milliseconds
# 取"累积占比首次 >= 目标"那一档的延迟当分位值。输出: min p50 p95 p99 max
lat(){
    tr '\r' '\n' <"$1" | awk '
        /<= *[0-9.]+ *milliseconds/ {
            pct = $1; sub(/%$/, "", pct)
            ms  = $3
            if (n == 0) min = ms
            if (p50 == "" && pct+0 >= 50) p50 = ms
            if (p95 == "" && pct+0 >= 95) p95 = ms
            if (p99 == "" && pct+0 >= 99) p99 = ms
            max = ms; n++
        }
        END{ if (n == 0) print "- - - - -"; else print min, p50, p95, p99, max }'
}

# 逐列取中位数(列数少,冒泡即可;容器里的 awk 不保证有 asort)
col_median(){
    awk '
        $1 == "" { next }
        { n++; for(c=1;c<=NF;c++) x[c,n]=$c; if(NF>maxf) maxf=NF }
        END{
            if (n == 0) { print "- - - - - -"; exit }
            for(c=1;c<=maxf;c++){
                m=0
                for(r=1;r<=n;r++) if((c,r) in x) t[++m]=x[c,r]
                for(a=1;a<=m;a++) for(b=a+1;b<=m;b++) if(t[b]<t[a]){q=t[a];t[a]=t[b];t[b]=q}
                printf "%s%s", (m%2)?t[(m+1)/2]:(t[m/2]+t[m/2+1])/2, (c<maxf)?" ":"\n"
            }
        }'
}

# measure <tag> <redis-benchmark 参数...>
# 每轮重启服务器 -> 播种键空间 -> 采 REPS 轮 -> 逐列取中位
# 输出: "qps min p50 p95 p99 max"
measure(){
    local tag="$1"; shift
    local i f
    for i in $(seq 1 "$REPS"); do
        start_srv || return 1
        # 播种:让 GET 打的是已存在的键,而不是未命中路径
        redis-benchmark -p "$PORT" -t set -n "$KEYSPACE" -c 50 -r "$KEYSPACE" -q >/dev/null 2>&1
        f="$RUN/r.$tag.$i"
        redis-benchmark -p "$PORT" "$@" >"$f" 2>/dev/null
        printf '%s %s\n' "$(rps "$f")" "$(lat "$f")"
    done | col_median
}

# ---------------- 构建 ----------------

if [ "${SKIP_BUILD:-0}" != "1" ]; then
    cmake -S "$REPO" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >"$RUN.cmake.log" 2>&1 \
        || { echo "!! cmake 失败"; tail -20 "$RUN.cmake.log"; exit 1; }
    cmake --build "$BUILD" -j"$(nproc)" >"$RUN.build.log" 2>&1 \
        || { echo "!! 编译失败"; tail -30 "$RUN.build.log"; exit 1; }
fi

echo "编译: $(grep -m1 'CXX_FLAGS' "$BUILD/CMakeFiles/miniredis_core.dir/flags.make" 2>/dev/null | sed 's/CXX_FLAGS = //')"
echo "机器: $(nproc) 核"
echo "服务: 单线程,AOF 开启(appendfsync everysec)"

# ---------------- 探活 ----------------
# 探针要真的看到数字才算过:不支持的类型 redis-benchmark 也是 exit 0
start_srv || exit 1
SUPPORTED=""
for t in ping_mbulk set get incr hset mset zadd zpopmin; do
    f="$RUN/probe.$t"
    redis-benchmark -p "$PORT" -t "$t" -n 2000 -c 10 -r 1000 >"$f" 2>/dev/null
    [ -n "$(rps "$f")" ] && SUPPORTED="$SUPPORTED $t"
done
stop_srv

printf '\n## 单命令吞吐\n\n'
printf '`-c 50 -n %s -r %s`,每测点 %s 次取中位;GET/INCR 前先播种 %s 个键。\n\n' \
    "$N_MAIN" "$KEYSPACE" "$REPS" "$KEYSPACE"
printf '| 命令 | QPS | min (ms) | p50 | p95 | p99 | max |\n'
printf '|---|---:|---:|---:|---:|---:|---:|\n'
for t in $SUPPORTED; do
    read -r q a b c d e <<EOF
$(measure "main.$t" -t "$t" -n "$N_MAIN" -c 50 -r "$KEYSPACE")
EOF
    printf '| %s | %s | %s | %s | %s | %s | %s |\n' "$t" "$q" "$a" "$b" "$c" "$d" "$e"
done

printf '\n## 并发梯度(SET / GET)\n\n'
printf '`-n %s -r %s`。\n\n' "$N_MAIN" "$KEYSPACE"
printf '| 并发 | SET QPS | SET p99 | GET QPS | GET p99 |\n'
printf '|---:|---:|---:|---:|---:|\n'
for c in 1 10 50 100 200; do
    n="$N_MAIN"; [ "$c" = "1" ] && n=$((N_MAIN / 4))
    out=""
    for t in set get; do
        read -r q _a _b _c p99 _e <<EOF
$(measure "conc.$t.$c" -t "$t" -n "$n" -c "$c" -r "$KEYSPACE")
EOF
        out="$out| $q | $p99 "
    done
    printf '| %s %s|\n' "$c" "$out"
done

printf '\n## 管道(pipeline)\n\n'
printf '`-n %s -c 50 -r %s`。\n\n' "$N_MAIN" "$KEYSPACE"
printf '| 管道深度 | SET QPS | GET QPS |\n'
printf '|---:|---:|---:|\n'
for p in 1 16 64; do
    out=""
    for t in set get; do
        read -r q _a _b _c _d _e <<EOF
$(measure "pipe.$t.$p" -t "$t" -n "$N_MAIN" -c 50 -P "$p" -r "$KEYSPACE")
EOF
        out="$out| $q "
    done
    printf '| %s %s|\n' "$p" "$out"
done

printf '\n## 值大小(SET / GET,`-c 50 -r %s`)\n\n' "$KEYSPACE"
printf '| 值大小 | 请求数 | SET QPS | GET QPS |\n'
printf '|---:|---:|---:|---:|\n'
for d in 3 64 1024 4096; do
    n=$N_MAIN; [ "$d" -gt 1024 ] && n=$((N_MAIN / 4))
    out=""
    for t in set get; do
        read -r q _a _b _c _d _e <<EOF
$(measure "size.$t.$d" -t "$t" -n "$n" -c 50 -d "$d" -r "$KEYSPACE")
EOF
        out="$out| $q "
    done
    printf '| %s B | %s %s|\n' "$d" "$n" "$out"
done

# ---------------- AOF 重写路径压力 ----------------
# 回归 aof.cpp:264 那个 O_CREAT 缺 mode 的 abort:持续写入逼出多轮重写,看进程活不活

printf '\n## AOF 重写路径压力(持续写入 15s)\n\n'
start_srv || exit 1
PID=$(srv_pids | head -1)
AOF="$RUN/appendonly.aof"
INODE=$(stat -c %i "$AOF" 2>/dev/null || echo "-")
REWRITES=0
( timeout 15 redis-benchmark -p "$PORT" -t set -n 5000000 -c 50 -r "$KEYSPACE" -q >/dev/null 2>&1 ) &
LOAD=$!
for i in $(seq 1 30); do
    sleep 0.5
    CUR=$(stat -c %i "$AOF" 2>/dev/null || echo "-")
    if [ "$CUR" != "$INODE" ]; then REWRITES=$((REWRITES + 1)); INODE="$CUR"; fi
done
wait $LOAD
sleep 0.5
ALIVE=$(srv_pids | head -1)

printf '| 指标 | 值 |\n|---|---:|\n'
printf '| 触发 AOF 重写次数 | %s |\n' "$REWRITES"
if [ -n "$ALIVE" ]; then printf '| 压测后进程 | 存活 (pid %s) |\n' "$ALIVE"
else printf '| 压测后进程 | **已退出** |\n'; fi
printf '| 残留 .tmp.bg 文件 | %s |\n' "$(ls "$RUN" | grep -c 'tmp.bg' || true)"
printf '| AOF 文件大小 | %s |\n' "$(du -h "$AOF" 2>/dev/null | cut -f1)"
printf '\n服务器日志(应为空):\n```\n%s\n```\n' "$(cat "$RUN/srv.log")"

# ---------------- 资源占用 ----------------

printf '\n## 资源占用\n\n'
start_srv || exit 1
PID=$(srv_pids | head -1)
FD_BEFORE=$(ls /proc/"$PID"/fd 2>/dev/null | wc -l)
RSS_BEFORE=$(awk '/VmRSS/{print $2}' /proc/"$PID"/status)
redis-benchmark -p "$PORT" -t set,get -n 400000 -c 100 -r "$KEYSPACE" -q >/dev/null 2>&1
sleep 0.5
FD_AFTER=$(ls /proc/"$PID"/fd 2>/dev/null | wc -l)
printf '| 指标 | 值 |\n|---|---:|\n'
printf '| fd 数(压测前 / 后) | %s / %s |\n' "$FD_BEFORE" "$FD_AFTER"
printf '| RSS(压测前 / 后) | %s kB / %s kB |\n' "$RSS_BEFORE" "$(awk '/VmRSS/{print $2}' /proc/"$PID"/status)"
printf '| 压测后进程 | %s |\n' "$(srv_pids >/dev/null && [ -n "$(srv_pids)" ] && echo 存活 || echo '**已退出**')"

stop_srv
printf '\n压测完成。\n'
