#include "load_balance.h"
#include "sylar/core/log.h"

#include <algorithm>
#include <cstdlib>

namespace sylar {
namespace rpc {

static Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

// ---- HolderStats ----

void HolderStats::clear() {
    m_usedTime = 0;
    m_total = 0;
    m_doing = 0;
    m_timeouts = 0;
    m_oks = 0;
    m_errs = 0;
}

float HolderStats::getWeight(float rate) {
    float base = (float)(m_total.load() + 20);
    return std::min((m_oks.load() * 1.0f / (m_usedTime.load() + 1)) * 2.0f, 50.0f)
           * (1 - 4.0f * m_timeouts.load() / base)
           * (1 - 1.0f * m_doing.load() / base)
           * (1 - 10.0f * m_errs.load() / base) * rate;
}

std::string HolderStats::toString() {
    return snapshot().toString();
}

HolderStats::Snapshot HolderStats::snapshot() const {
    Snapshot s;
    s.usedTime = m_usedTime.load();
    s.total = m_total.load();
    s.doing = m_doing.load();
    s.timeouts = m_timeouts.load();
    s.oks = m_oks.load();
    s.errs = m_errs.load();
    return s;
}

float HolderStats::Snapshot::getWeight(float rate) const {
    float base = (float)(total + 20);
    return std::min((oks * 1.0f / (usedTime + 1)) * 2.0f, 50.0f)
           * (1 - 4.0f * timeouts / base)
           * (1 - 1.0f * doing / base)
           * (1 - 10.0f * errs / base) * rate;
}

std::string HolderStats::Snapshot::toString() const {
    std::stringstream ss;
    ss << "[Stat total=" << total << " oks=" << oks << " errs=" << errs
       << " timeouts=" << timeouts << " doing=" << doing
       << " weight=" << getWeight(1) << "]";
    return ss.str();
}

// ---- HolderStatsSet ----

HolderStatsSet::HolderStatsSet(uint32_t size) {
    m_stats.resize(size);
}

void HolderStatsSet::init(const uint32_t& now) {
    if(m_lastUpdateTime < now) {
        for(uint32_t t = m_lastUpdateTime + 1, i = 0;
            t <= now && i < m_stats.size(); ++t, ++i) {
            m_stats[t % m_stats.size()].clear();
        }
        m_lastUpdateTime = now;
    }
}

HolderStats& HolderStatsSet::get(const uint32_t& now) {
    init(now);
    return m_stats[now % m_stats.size()];
}

float HolderStatsSet::getWeight(const uint32_t& now) {
    init(now);
    float v = 0;
    for(size_t i = 1; i < m_stats.size(); ++i) {
        v += m_stats[(now - i) % m_stats.size()].getWeight(1 - 0.1f * i);
    }
    return v;
}

HolderStats::Snapshot HolderStatsSet::getTotal() {
    HolderStats::Snapshot rt;
    for(auto& i : m_stats) {
        rt.usedTime += i.getUsedTime();
        rt.total += i.getTotal();
        rt.doing += i.getDoing();
        rt.timeouts += i.getTimeouts();
        rt.oks += i.getOks();
        rt.errs += i.getErrs();
    }
    return rt;
}

// ---- LoadBalanceItem ----

HolderStats& LoadBalanceItem::get(const uint32_t& now) {
    return m_stats.get(now);
}

std::string LoadBalanceItem::toString() {
    std::stringstream ss;
    ss << "[Item id=" << m_id << " weight=" << getWeight()
       << m_stats.getTotal().toString() << "]";
    return ss.str();
}

// ---- FairLoadBalanceItem ----

int32_t FairLoadBalanceItem::getWeight() {
    int32_t v = (int32_t)(m_weight * m_stats.getWeight());
    return v > 1 ? v : 1;
}

// ---- LoadBalance ----

LoadBalanceItem::ptr LoadBalance::getById(uint64_t id) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_datas.find(id);
    return it == m_datas.end() ? nullptr : it->second;
}

void LoadBalance::add(LoadBalanceItem::ptr v) {
    RWMutexType::WriteLock lock(m_mutex);
    m_datas[v->getId()] = v;
    initNolock();
}

void LoadBalance::del(LoadBalanceItem::ptr v) {
    RWMutexType::WriteLock lock(m_mutex);
    m_datas.erase(v->getId());
    initNolock();
}

void LoadBalance::update(
    const std::unordered_map<uint64_t, LoadBalanceItem::ptr>& adds,
    std::unordered_map<uint64_t, LoadBalanceItem::ptr>& dels) {
    RWMutexType::WriteLock lock(m_mutex);
    for(auto& i : dels) {
        auto it = m_datas.find(i.first);
        if(it != m_datas.end()) {
            i.second = it->second;
            m_datas.erase(it);
        }
    }
    for(auto& i : adds) {
        m_datas[i.first] = i.second;
    }
    initNolock();
}

void LoadBalance::set(const std::vector<LoadBalanceItem::ptr>& vs) {
    RWMutexType::WriteLock lock(m_mutex);
    m_datas.clear();
    for(auto& i : vs) {
        m_datas[i->getId()] = i;
    }
    initNolock();
}

void LoadBalance::init() {
    RWMutexType::WriteLock lock(m_mutex);
    initNolock();
}

std::string LoadBalance::statusString(const std::string& prefix) {
    RWMutexType::ReadLock lock(m_mutex);
    decltype(m_datas) datas = m_datas;
    lock.unlock();
    std::stringstream ss;
    ss << prefix << "init_time: " << sylar::Time2Str(m_lastInitTime / 1000) << std::endl;
    for(auto& i : datas) {
        ss << prefix << i.second->toString() << std::endl;
    }
    return ss.str();
}

void LoadBalance::checkInit() {
    uint64_t ts = sylar::GetCurrentMS();
    if(ts - m_lastInitTime > 500) {
        init();
        m_lastInitTime = ts;
    }
}

// ---- RoundRobinLoadBalance ----

void RoundRobinLoadBalance::initNolock() {
    decltype(m_items) items;
    for(auto& i : m_datas) {
        if(i.second->isValid()) {
            items.push_back(i.second);
        }
    }
    items.swap(m_items);
}

LoadBalanceItem::ptr RoundRobinLoadBalance::get(uint64_t v) {
    checkInit();
    RWMutexType::ReadLock lock(m_mutex);
    if(m_items.empty()) {
        return nullptr;
    }
    uint32_t r = (v == (uint64_t)-1 ? rand() : (uint32_t)v) % m_items.size();
    for(size_t i = 0; i < m_items.size(); ++i) {
        auto& h = m_items[(r + i) % m_items.size()];
        if(h->isValid()) {
            return h;
        }
    }
    return nullptr;
}

// ---- WeightLoadBalance ----

FairLoadBalanceItem::ptr WeightLoadBalance::getAsFair() {
    auto item = get();
    if(item) {
        return std::static_pointer_cast<FairLoadBalanceItem>(item);
    }
    return nullptr;
}

LoadBalanceItem::ptr WeightLoadBalance::get(uint64_t v) {
    checkInit();
    RWMutexType::ReadLock lock(m_mutex);
    int32_t idx = getIdx(v);
    if(idx == -1) {
        return nullptr;
    }
    for(size_t i = 0; i < m_items.size(); ++i) {
        auto& h = m_items[(idx + i) % m_items.size()];
        if(h->isValid()) {
            return h;
        }
    }
    return nullptr;
}

void WeightLoadBalance::initNolock() {
    decltype(m_items) items;
    for(auto& i : m_datas) {
        if(i.second->isValid()) {
            items.push_back(i.second);
        }
    }
    items.swap(m_items);

    int64_t total = 0;
    m_weights.resize(m_items.size());
    for(size_t i = 0; i < m_items.size(); ++i) {
        total += m_items[i]->getWeight();
        m_weights[i] = total;
    }
}

int32_t WeightLoadBalance::getIdx(uint64_t v) {
    if(m_weights.empty()) {
        return -1;
    }
    int64_t total = *m_weights.rbegin();
    if(total <= 0) {
        return -1;
    }
    uint64_t dis = (v == (uint64_t)-1 ? rand() : v) % total;
    auto it = std::upper_bound(m_weights.begin(), m_weights.end(), (int64_t)dis);
    if(it == m_weights.end()) {
        return (int32_t)(m_weights.size() - 1);
    }
    return (int32_t)std::distance(m_weights.begin(), it);
}

} // namespace rpc
} // namespace sylar
