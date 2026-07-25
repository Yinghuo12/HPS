#ifndef __SYLAR_RPC_LOAD_BALANCE_H__
#define __SYLAR_RPC_LOAD_BALANCE_H__

#include <atomic>
#include <cmath>
#include <ctime>
#include <list>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "sylar/core/thread.h"
#include "sylar/core/sys_util.h"

namespace sylar {
namespace rpc {

// 连接统计指标(原子操作, 线程安全)
// 统计一个连接实例的: 总请求数/成功数/错误数/超时数/正在处理数/累计耗时
class HolderStats {
    friend class HolderStatsSet;
public:
    HolderStats() = default;
    HolderStats(const HolderStats& o) {
        m_usedTime.store(o.m_usedTime.load());
        m_total.store(o.m_total.load());
        m_doing.store(o.m_doing.load());
        m_timeouts.store(o.m_timeouts.load());
        m_oks.store(o.m_oks.load());
        m_errs.store(o.m_errs.load());
    }
    HolderStats& operator=(const HolderStats& o) {
        if(this != &o) {
            m_usedTime.store(o.m_usedTime.load());
            m_total.store(o.m_total.load());
            m_doing.store(o.m_doing.load());
            m_timeouts.store(o.m_timeouts.load());
            m_oks.store(o.m_oks.load());
            m_errs.store(o.m_errs.load());
        }
        return *this;
    }
    uint32_t getUsedTime() const { return m_usedTime.load(); }
    uint32_t getTotal() const { return m_total.load(); }
    uint32_t getDoing() const { return m_doing.load(); }
    uint32_t getTimeouts() const { return m_timeouts.load(); }
    uint32_t getOks() const { return m_oks.load(); }
    uint32_t getErrs() const { return m_errs.load(); }

    uint32_t incUsedTime(uint32_t v) { return m_usedTime.fetch_add(v) + v; }
    uint32_t incTotal(uint32_t v) { return m_total.fetch_add(v) + v; }
    uint32_t incDoing(uint32_t v) { return m_doing.fetch_add(v) + v; }
    uint32_t incTimeouts(uint32_t v) { return m_timeouts.fetch_add(v) + v; }
    uint32_t incOks(uint32_t v) { return m_oks.fetch_add(v) + v; }
    uint32_t incErrs(uint32_t v) { return m_errs.fetch_add(v) + v; }
    uint32_t decDoing(uint32_t v) { return m_doing.fetch_sub(v) - v; }

    void clear();
    float getWeight(float rate = 1.0f);
    std::string toString();

    // 用于 getTotal 汇总(非原子, 纯数据)
    struct Snapshot {
        uint32_t usedTime = 0;
        uint32_t total = 0;
        uint32_t doing = 0;
        uint32_t timeouts = 0;
        uint32_t oks = 0;
        uint32_t errs = 0;
        float getWeight(float rate = 1.0f) const;
        std::string toString() const;
    };
    Snapshot snapshot() const;

private:
    std::atomic<uint32_t> m_usedTime{0};
    std::atomic<uint32_t> m_total{0};
    std::atomic<uint32_t> m_doing{0};
    std::atomic<uint32_t> m_timeouts{0};
    std::atomic<uint32_t> m_oks{0};
    std::atomic<uint32_t> m_errs{0};
};

// 时间窗口统计集(按秒分片, 最近 N 秒的 HolderStats)
// getWeight() 对最近 N 秒的统计数据做加权平均(越近权重越高)。
class HolderStatsSet {
public:
    explicit HolderStatsSet(uint32_t size = 5);
    HolderStats& get(const uint32_t& now = time(nullptr));
    float getWeight(const uint32_t& now = time(nullptr));
    HolderStats::Snapshot getTotal();

private:
    void init(const uint32_t& now);

    uint32_t m_lastUpdateTime = 0;
    std::vector<HolderStats> m_stats;
};

// 负载均衡项: 封装一个连接实例 + 统计数据
class LoadBalanceItem {
public:
    typedef std::shared_ptr<LoadBalanceItem> ptr;

    virtual ~LoadBalanceItem() {}

    void setId(uint64_t v) { m_id = v; }
    uint64_t getId() const { return m_id; }

    HolderStats& get(const uint32_t& now = time(nullptr));

    virtual int32_t getWeight() { return m_weight; }
    void setWeight(int32_t v) { m_weight = v; }

    virtual bool isValid() { return true; }
    virtual void close() {}

    std::string toString();

protected:
    uint64_t m_id = 0;
    int32_t m_weight = 0;
    HolderStatsSet m_stats;
};

// 负载均衡策略接口
class ILoadBalance {
public:
    enum Type { ROUNDROBIN = 1, WEIGHT = 2, FAIR = 3 };
    enum Error { NO_SERVICE = -101, NO_CONNECTION = -102 };

    typedef std::shared_ptr<ILoadBalance> ptr;
    virtual ~ILoadBalance() {}
    virtual LoadBalanceItem::ptr get(uint64_t v = (uint64_t)-1) = 0;
};

// 负载均衡基类: 管理所有实例 + 定期重排
class LoadBalance : public ILoadBalance {
public:
    typedef sylar::RWMutex RWMutexType;
    typedef std::shared_ptr<LoadBalance> ptr;

    void add(LoadBalanceItem::ptr v);
    void del(LoadBalanceItem::ptr v);
    void set(const std::vector<LoadBalanceItem::ptr>& vs);
    LoadBalanceItem::ptr getById(uint64_t id);
    void update(
        const std::unordered_map<uint64_t, LoadBalanceItem::ptr>& adds,
        std::unordered_map<uint64_t, LoadBalanceItem::ptr>& dels);
    void init();
    std::string statusString(const std::string& prefix = "");

protected:
    virtual void initNolock() = 0;
    void checkInit();

    RWMutexType m_mutex;
    std::unordered_map<uint64_t, LoadBalanceItem::ptr> m_datas;
    uint64_t m_lastInitTime = 0;
};

// 轮询: 随机起点 + 顺序遍历
class RoundRobinLoadBalance : public LoadBalance {
public:
    typedef std::shared_ptr<RoundRobinLoadBalance> ptr;
    virtual LoadBalanceItem::ptr get(uint64_t v = (uint64_t)-1) override;

protected:
    virtual void initNolock() override;

protected:
    std::vector<LoadBalanceItem::ptr> m_items;
};

// Fair 负载均衡项: 动态权重 = 静态权重 × 统计权重
class FairLoadBalanceItem : public LoadBalanceItem {
public:
    typedef std::shared_ptr<FairLoadBalanceItem> ptr;
    virtual int32_t getWeight() override;
};

// 权重负载均衡: 支持静态权重(WEIGHT)和动态权重(FAIR)
// FAIR 策略用 FairLoadBalanceItem, WEIGHT 用普通 LoadBalanceItem。
class WeightLoadBalance : public LoadBalance {
public:
    typedef std::shared_ptr<WeightLoadBalance> ptr;
    virtual LoadBalanceItem::ptr get(uint64_t v = (uint64_t)-1) override;
    FairLoadBalanceItem::ptr getAsFair();

protected:
    virtual void initNolock() override;

private:
    int32_t getIdx(uint64_t v = (uint64_t)-1);

protected:
    std::vector<LoadBalanceItem::ptr> m_items;

private:
    std::vector<int64_t> m_weights;
};

} // namespace rpc
} // namespace sylar

#endif
