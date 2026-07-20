#!/bin/bash
# fleet.sh — 5 服务一键启停/状态（带 etcd close 垫片）; 用法: {start|stop|status|etcd|logs [name] [n]}
set -u
ROOT=/home/yinghuo/code/proj/sylar
PRE=$ROOT/lib/libetcd_close_fix.so
CONF=$ROOT/src/server/conf
BIN=$ROOT/bin
LOG=/tmp
SERVS=(login gate lobby battle data)

# 进程匹配模式: 按二进制名精确匹配所有路径形式(含 ./ddt_xxx 相对路径)
PATTERN="ddt_(login|gate|lobby|battle|data)[ ]"

# 杀掉所有 5 服务进程(不限路径), 重试 2 次确保杀干净
kill_all_ddt() {
    pkill -f "$PATTERN" 2>/dev/null || true
    sleep 1
    # 兜底: 仍残留的强杀
    if pgrep -f "$PATTERN" >/dev/null 2>&1; then
        pkill -9 -f "$PATTERN" 2>/dev/null || true
        sleep 1
    fi
}

case "${1:-status}" in
    restart)
        # 编译 + 重启: 强制重新编译 libsylar + 5 个服务, 然后重启全部
        echo "== rebuild =="
        cd "$ROOT/build"
        # 强制删除 .o 确保重新编译(避免 CMake 增量检测遗漏)
        rm -f CMakeFiles/sylar.dir/sylar/**/*.o 2>/dev/null
        cmake --build . --target sylar ddt_gate ddt_login ddt_lobby ddt_battle ddt_data -- -j"$(nproc)" 2>&1 | grep -iE "error|Built target" || { echo "build FAIL"; exit 1; }
        echo "== restart =="
        ;&  # fall through 到 start
    start)
        # 清旧进程(所有路径形式, 含 ./ddt_xxx 相对路径启动的残留)
        kill_all_ddt
        # 开启 core dump(排查 stack smashing 崩溃用)
        ulimit -c unlimited
        for s in "${SERVS[@]}"; do
            cd "$BIN" && nohup env LD_PRELOAD="$PRE" bash -c "ulimit -c unlimited; exec $BIN/ddt_$s -c $CONF/$s.yml" >"$LOG/ddt_$s.log" 2>&1 &
            echo "started ddt_$s pid=$!"
        done
        echo "waiting 4s for bind + etcd register..."
        sleep 4
        ;;
    stop)
        kill_all_ddt
        echo "stopped all ddt_* (login/gate/lobby/battle/data, all path forms)"
        ;;
    status)
        echo "== processes =="
        pgrep -af "$PATTERN" 2>/dev/null || echo "(none)"
        echo "== listening ports =="
        ss -ltn 2>/dev/null | grep -E ':810[01]|:820[01]|:830[01]|:840[01]|:850[01]' | sort || echo "(none)"
        ;;
    etcd)
        echo "== etcd /-prefixed keys (service registrations) =="
        curl -s http://127.0.0.1:2379/v3/kv/range -X POST \
            -d '{"key":"Lw==","range_end":"MA=="}' | python3 -c '
import sys,json,base64
d=json.load(sys.stdin)
kvs=d.get("kvs",[])
print(f"count={len(kvs)}")
for k in kvs:
    key=base64.b64decode(k["key"]).decode("utf-8","replace")
    val=base64.b64decode(k.get("value","")).decode("utf-8","replace") if "value" in k else ""
    print(f"  {key}  ->  {val}")
'
        ;;
    logs)
        s="${2:-gate}"
        echo "== /tmp/ddt_$s.log =="
        tail -n "${3:-30}" "$LOG/ddt_$s.log" 2>/dev/null || echo "(no log)"
        ;;
    *)
        echo "usage: $0 {start|stop|status|etcd|logs [name] [n]}"
        exit 1
        ;;
esac
