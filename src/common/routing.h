#ifndef __DDT_ROUTING_H__
#define __DDT_ROUTING_H__

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ddt {

// 稳定路由句柄
// battle/lobby 推送消息给客户端时不持有客户端连接(sylar::Socket),
// 而是持有一个 RoutingHandle, 通过 PushService.NotifyClient RPC 把消息
// 推回该玩家所在的 gate, gate 再转发到本地 session
//   accountId : 全局唯一玩家账号 ID(贯穿所有服务)
//   gatewayId : 该玩家当前连接的 gate 实例标识(预留; 多 gate 部署时用于
//               选择 PushService 的目标 gate, 当前单 gate 时统一)
struct RoutingHandle {
    uint64_t accountId = 0;
    uint64_t gatewayId = 0;

    RoutingHandle() = default;
    RoutingHandle(uint64_t aid, uint64_t gid)
        : accountId(aid), gatewayId(gid) {}

    bool valid() const { return accountId != 0; }
};

// 服务间推送闭包: 调用方在 lobby/battle 内构造,
// 内部即一次 PushService.NotifyClient(accountId, msg_id, payload) 的 RPC
// msg_id 见 msg_id.h, payload 为对应消息的 protobuf 序列化字节
typedef std::function<void(const RoutingHandle&, uint16_t msg_id, const std::string& payload)> PushFn;

// 批量推送闭包: 一次 RPC 把同一条消息推给多个玩家
// 内部即一次 PushService.NotifyClients(account_ids, msg_id, payload) 的 RPC
// 用于房间广播: 原来 N 个玩家 = N 次 RPC(N 个 fiber), 现在 1 次 RPC(1 个 fiber)
typedef std::function<void(const std::vector<uint64_t>& account_ids, uint16_t msg_id, const std::string& payload)> BroadcastPushFn;

} // namespace ddt

#endif
