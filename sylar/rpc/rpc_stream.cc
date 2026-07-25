#include "rpc_stream.h"
#include "sylar/core/log.h"

namespace sylar {
namespace rpc {

static Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

// doSend: 发送请求包 [4B header_size][RpcHeader][args]
bool RpcStream::RpcCtx::doSend(AsyncSocketStream::ptr stream) {
    // 请求包已在外部构造好, 存在 sendCtx 的成员里。
    // 但 SendCtx::doSend 只接收 stream 参数, 需要把包数据存在 RpcCtx 里。
    // 这里不使用——rpc_channel 的 CallMethod 直接调 enqueue, 请求包存在 RpcCtx 成员。
    return true;
}

// doRecv: 阻塞读 [4B total_size][4B request_id][body], 匹配 request_id 返回 Ctx。
AsyncSocketStream::Ctx::ptr RpcStream::doRecv() {
    // 读 4B total_size
    uint32_t totalSize = 0;
    if(readFixSize(&totalSize, sizeof(totalSize)) <= 0) {
        return nullptr;
    }
    totalSize = sylar::byteswapOnLittleEndian(totalSize);

    // 边界保护
    if(totalSize < sizeof(uint32_t) || totalSize > 64u * 1024 * 1024) {
        SYLAR_LOG_ERROR(g_logger) << "RpcStream doRecv: bad total_size=" << totalSize;
        return nullptr;
    }

    // 读 4B request_id
    uint32_t reqId = 0;
    if(readFixSize(&reqId, sizeof(reqId)) <= 0) {
        return nullptr;
    }
    reqId = sylar::byteswapOnLittleEndian(reqId);

    // 读 response body
    uint32_t bodySize = totalSize - sizeof(uint32_t);
    std::string body;
    if(bodySize > 0) {
        body.resize(bodySize);
        if(readFixSize(&body[0], bodySize) <= 0) {
            return nullptr;
        }
    }

    // 按 request_id 查找等待的 Ctx
    auto ctx = getAndDelCtx(reqId);
    if(!ctx) {
        SYLAR_LOG_WARN(g_logger) << "RpcStream doRecv: no pending ctx for reqId=" << reqId;
        return nullptr;
    }

    // 填充 response body 到 RpcCtx
    RpcCtx::ptr rpcCtx = std::dynamic_pointer_cast<RpcCtx>(ctx);
    if(rpcCtx) {
        rpcCtx->recvBody = std::move(body);
        if(rpcCtx->response && !rpcCtx->recvBody.empty()) {
            rpcCtx->response->ParseFromString(rpcCtx->recvBody);
        }
    }

    return ctx;
}

} // namespace rpc
} // namespace sylar
