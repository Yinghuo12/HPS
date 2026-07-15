// test_etcd.cc — 验证 sylar::rpc 的 etcd 封装（EtcdClient / EtcdRegistrar / EtcdWatcher）
// 对真实 etcd server (127.0.0.1:2379) 做端到端测试。
//
// 运行前置：etcd 已在本机 2379 启动（单节点 dev 模式即可）。
// 编译：该文件只引用 sylar/rpc/etcd_client.h（C++11 干净头），C++11 即可；
//       etcd-cpp-apiv3(C++17) 已编进 libsylar，通过链接 sylar 传递。

#include "sylar/rpc/etcd_client.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "sylar/core/log.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

static const std::string kEndpoint = "http://127.0.0.1:2379";

// ---- 极简断言框架 ----------------------------------------------------------
static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (cond) {                                                             \
            ++g_pass;                                                           \
            SYLAR_LOG_INFO(g_logger) << "[ PASS ] " << msg;                     \
        } else {                                                                \
            ++g_fail;                                                           \
            SYLAR_LOG_ERROR(g_logger) << "[ FAIL ] " << msg;                    \
        }                                                                       \
    } while (0)

static void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ---------------------------------------------------------------------------
// 1. EtcdClient 基础 KV：put / get / 覆盖 / del / 不存在
// ---------------------------------------------------------------------------
static void test_kv_basics() {
    SYLAR_LOG_INFO(g_logger) << "\n========== [1] EtcdClient KV basics ==========";
    sylar::rpc::EtcdClient cli(kEndpoint);
    sylar::rpc::EtcdClient::KV kv;

    // 干净起点
    cli.del("/test_etcd/kv/hello");

    CHECK(cli.put("/test_etcd/kv/hello", "world"), "put hello=world");
    CHECK(cli.get("/test_etcd/kv/hello", kv) && kv.value == "world",
          "get hello == world (got='" << kv.value << "')");

    CHECK(cli.put("/test_etcd/kv/hello", "world2"), "overwrite hello=world2");
    CHECK(cli.get("/test_etcd/kv/hello", kv) && kv.value == "world2",
          "get hello == world2 after overwrite");

    CHECK(!cli.get("/test_etcd/kv/nope", kv), "get nonexistent returns false");

    CHECK(cli.del("/test_etcd/kv/hello"), "del hello");
    CHECK(!cli.get("/test_etcd/kv/hello", kv), "get after del returns false");

    CHECK(!cli.del("/test_etcd/kv/nope"), "del nonexistent returns false");
}

// ---------------------------------------------------------------------------
// 2. getPrefix / delPrefix：前缀枚举（服务发现的核心）
// ---------------------------------------------------------------------------
static void test_prefix() {
    SYLAR_LOG_INFO(g_logger) << "\n========== [2] getPrefix / delPrefix ==========";
    sylar::rpc::EtcdClient cli(kEndpoint);

    cli.delPrefix("/test_etcd/pfx/");   // 干净起点

    // 模拟多实例注册：/svc/FriendService/GetList -> ip:port
    cli.put("/test_etcd/pfx/svc/Friend/Get/inst1", "10.0.0.1:9000");
    cli.put("/test_etcd/pfx/svc/Friend/Get/inst2", "10.0.0.2:9000");
    cli.put("/test_etcd/pfx/svc/Friend/Get/inst3", "10.0.0.3:9000");

    std::vector<sylar::rpc::EtcdClient::KV> out;
    CHECK(cli.getPrefix("/test_etcd/pfx/svc/Friend/Get/", out) && out.size() == 3,
          "getPrefix returns 3 instances (got=" << out.size() << ")");
    for (auto& e : out) {
        SYLAR_LOG_INFO(g_logger) << "    instance: " << e.key << " -> " << e.value;
    }

    CHECK(cli.delPrefix("/test_etcd/pfx/"), "delPrefix /test_etcd/pfx/");
    out.clear();
    CHECK(cli.getPrefix("/test_etcd/pfx/", out) && out.empty(),
          "after delPrefix, getPrefix empty (got=" << out.size() << ")");
}

// ---------------------------------------------------------------------------
// 3. 裸 Lease（EtcdClient）：租约到期后键自动删除 —— 等价 ZK 临时节点
//    注意：EtcdClient::leaseKeepalive 实现里只调 leasetimetolive 探活，
//    并不真正续租，所以这里租约会如期过期。
// ---------------------------------------------------------------------------
static void test_lease_expiry() {
    SYLAR_LOG_INFO(g_logger) << "\n========== [3] bare lease expiry (auto-delete) ==========";
    sylar::rpc::EtcdClient cli(kEndpoint);

    int64_t lease = cli.leaseGrant(2);   // 2 秒租约
    CHECK(lease > 0, "leaseGrant(2) returns id>0 (got=" << lease << ")");

    int64_t ttl = cli.leaseKeepalive(lease);
    CHECK(ttl > 0, "leaseKeepalive (probe) returns remaining ttl>0 right after grant (got=" << ttl << ")");

    CHECK(cli.put("/test_etcd/lease/ephemeral", "v", lease),
          "put ephemeral key bound to lease");

    sylar::rpc::EtcdClient::KV kv;
    CHECK(cli.get("/test_etcd/lease/ephemeral", kv) && kv.lease == lease,
          "ephemeral key present & lease field matches (lease=" << kv.lease << ")");

    SYLAR_LOG_INFO(g_logger) << "    sleep 3.5s, wait for lease(2s) to expire...";
    sleep_ms(3500);

    CHECK(!cli.get("/test_etcd/lease/ephemeral", kv),
          "after lease expiry, key auto-deleted (get fails)");

    int64_t ttl2 = cli.leaseKeepalive(lease);
    CHECK(ttl2 == 0, "leaseKeepalive returns 0 after lease expired (got=" << ttl2 << ")");
}

// ---------------------------------------------------------------------------
// 4. EtcdRegistrar：KeepAlive 后台自动续租 —— 与裸 lease 对照
//    ttl=5，sleep 8s 后键【仍在】，证明续租生效；unregister 后键消失。
// ---------------------------------------------------------------------------
static void test_registrar_keepalive() {
    SYLAR_LOG_INFO(g_logger) << "\n========== [4] EtcdRegistrar KeepAlive (auto-renew) ==========";
    sylar::rpc::EtcdClient probe(kEndpoint);   // 用独立 client 观测
    sylar::rpc::EtcdClient::KV kv;

    probe.del("/test_etcd/reg/Friend/Get/inst1");

    {
        sylar::rpc::EtcdRegistrar reg(kEndpoint);
        sylar::rpc::EtcdRegisterInfo info;
        info.key = "/test_etcd/reg/Friend/Get/inst1";
        info.value = "10.0.0.9:9000";
        info.ttl = 5;

        CHECK(reg.registerService(info), "registerService ttl=5");

        CHECK(probe.get(info.key, kv) && kv.value == "10.0.0.9:9000",
              "registered key visible to observer (got='" << kv.value << "')");

        SYLAR_LOG_INFO(g_logger) << "    sleep 8s (>ttl=5), KeepAlive should have renewed...";
        sleep_ms(8000);

        CHECK(probe.get(info.key, kv) && kv.value == "10.0.0.9:9000",
              "key STILL present after 8s -> KeepAlive works (else would've expired at 5s)");

        // 未注销时重复注册同名 key 应拒绝
        CHECK(!reg.registerService(info), "duplicate register (same key, not unregistered) returns false");

        CHECK(reg.unregisterService(info.key), "unregisterService");
        CHECK(!probe.get(info.key, kv), "after unregister, key gone");

        // 注销后允许重新注册（entries 已清，应成功）
        CHECK(reg.registerService(info), "re-register after unregister succeeds");
        CHECK(reg.unregisterService(info.key), "cleanup re-registered key");
    }
    // 离开作用域 -> ~EtcdRegistrar() 调 clear()
}

// ---------------------------------------------------------------------------
// 4b. EtcdRegistrar 析构自清理：注册后不手动 unregister，析构应删键
// ---------------------------------------------------------------------------
static void test_registrar_dtor() {
    SYLAR_LOG_INFO(g_logger) << "\n========== [4b] EtcdRegistrar destructor cleanup ==========";
    sylar::rpc::EtcdClient probe(kEndpoint);
    sylar::rpc::EtcdClient::KV kv;
    probe.del("/test_etcd/reg/dtor/inst1");

    {
        sylar::rpc::EtcdRegistrar reg(kEndpoint);
        sylar::rpc::EtcdRegisterInfo info;
        info.key = "/test_etcd/reg/dtor/inst1";
        info.value = "10.0.0.8:9000";
        info.ttl = 30;
        CHECK(reg.registerService(info), "register for dtor test");
        CHECK(probe.get(info.key, kv), "key present before registrar destroyed");
    }   // 析构

    sleep_ms(500);
    CHECK(!probe.get("/test_etcd/reg/dtor/inst1", kv),
          "after ~EtcdRegistrar, key auto-removed (clear on dtor)");
}

// ---------------------------------------------------------------------------
// 5. EtcdWatcher：前缀监听，put/delete 触发异步回调
// ---------------------------------------------------------------------------
static void test_watcher() {
    SYLAR_LOG_INFO(g_logger) << "\n========== [5] EtcdWatcher async events ==========";
    sylar::rpc::EtcdClient cli(kEndpoint);
    cli.delPrefix("/test_etcd/watch/");

    sylar::rpc::EtcdWatcher watcher(kEndpoint);

    std::atomic<int> put_cnt{0};
    std::atomic<int> del_cnt{0};
    std::string last_key, last_val;

    sylar::rpc::EtcdWatchInfo wi;
    wi.prefix = "/test_etcd/watch/";
    wi.cb = [&](sylar::rpc::EtcdWatchEvent ev, const std::string& k, const std::string& v) {
        // 回调在 cpprest 工作线程触发，只动 atomic + 简单赋值
        if (ev == sylar::rpc::ETCD_PUT) { ++put_cnt; last_key = k; last_val = v; }
        else if (ev == sylar::rpc::ETCD_DELETE) { ++del_cnt; }
        SYLAR_LOG_INFO(g_logger) << "    [watch event] ev=" << ev
            << " key='" << k << "' val='" << v << "'";
    };
    CHECK(watcher.watch(wi), "watch prefix /test_etcd/watch/");

    sleep_ms(300);   // 等 watcher 建立 gRPC stream
    CHECK(cli.put("/test_etcd/watch/k1", "v1"), "trigger PUT k1=v1");
    sleep_ms(800);
    CHECK(put_cnt.load() == 1, "received 1 PUT event (got=" << put_cnt.load() << ")");
    CHECK(last_key == "/test_etcd/watch/k1" && last_val == "v1",
          "PUT event carries key/value (k='" << last_key << "' v='" << last_val << "')");

    CHECK(cli.del("/test_etcd/watch/k1"), "trigger DEL k1");
    sleep_ms(800);
    CHECK(del_cnt.load() == 1, "received 1 DELETE event (got=" << del_cnt.load() << ")");

    // 重复 watch 同前缀应拒绝
    CHECK(!watcher.watch(wi), "duplicate watch same prefix returns false");

    watcher.cancelAll();
}

int main(int argc, char** argv) {
    // root logger 默认已挂 StdoutLogAppender，无需重复添加

    SYLAR_LOG_INFO(g_logger) << "=== sylar::rpc etcd 端到端测试开始，endpoint=" << kEndpoint << " ===";

    test_kv_basics();
    test_prefix();
    test_lease_expiry();
    test_registrar_keepalive();
    test_registrar_dtor();
    test_watcher();

    SYLAR_LOG_INFO(g_logger) << "\n========================================";
    SYLAR_LOG_INFO(g_logger) << "RESULT: " << g_pass << " passed, " << g_fail << " failed";
    SYLAR_LOG_INFO(g_logger) << "========================================";
    return g_fail == 0 ? 0 : 1;
}
