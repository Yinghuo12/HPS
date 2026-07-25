#ifndef __SYLAR_RPC_RPC_STREAM_H__
#define __SYLAR_RPC_RPC_STREAM_H__

#include <string>
#include <atomic>

#include "rpcheader.pb.h"
#include "sylar/net/async_socket_stream.h"
#include "sylar/core/endian.h"

namespace sylar {
namespace rpc {

// RPC 异步流: 继承 AsyncSocketStream, 实现 doRecv 解析 RPC 响应。
// 响应格式(与 rpc_provider::sendResponse 一致):
//   [4B total_size (BE)][4B request_id (BE)][response protobuf body]
class RpcStream : public AsyncSocketStream {
public:
    typedef std::shared_ptr<RpcStream> ptr;

    RpcStream(Socket::ptr sock, bool owner = true)
        : AsyncSocketStream(sock, owner) {
    }

    // Ctx 扩展: 保存 response 指针, doRecv 收到匹配响应后填充。
    struct RpcCtx : public Ctx {
        typedef std::shared_ptr<RpcCtx> ptr;
        google::protobuf::Message* response = nullptr;  // 指向调用方的 response
        std::string recvBody;                           // 接收到的 response body

        bool doSend(AsyncSocketStream::ptr stream) override;
    };

protected:
    // doRead 协程调用: 阻塞读完整响应, 解析 request_id, 返回对应 Ctx。
    Ctx::ptr doRecv() override;
};

} // namespace rpc
} // namespace sylar

#endif
