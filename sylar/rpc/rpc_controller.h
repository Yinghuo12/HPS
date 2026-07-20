#ifndef __SYLAR_RPC_RPC_CONTROLLER_H__
#define __SYLAR_RPC_RPC_CONTROLLER_H__

#include <google/protobuf/service.h>
#include <memory>
#include <string>

namespace sylar {
namespace rpc {

// RPC 调用控制器：承载一次 RPC 的失败状态、取消信号与 traceId。
class RpcController : public google::protobuf::RpcController {
public:
    typedef std::shared_ptr<RpcController> ptr;

    RpcController();
    ~RpcController();

    void Reset() override;
    bool Failed() const override;
    std::string ErrorText() const override;
    void SetFailed(const std::string& reason) override;
    void StartCancel() override;
    bool IsCanceled() const override;
    void NotifyOnCancel(google::protobuf::Closure* callback) override;

    // traceId: caller(gate) 经 SetTraceId 注入，RpcChannel 序列化时读取并写入 RpcHeader.trace_id;
    //          callee(RpcProvider) 解析 RpcHeader 后通过 SetTraceId 注入到本 controller,
    //          impl 端通过 TraceId() 读取用于日志关联。空串 = 未启用。
    void SetTraceId(const std::string& id) { m_traceId = id; }
    const std::string& TraceId() const { return m_traceId; }

private:
    bool m_failed;
    std::string m_errText;
    std::string m_traceId;  // 调用链追踪 ID
};

}  // namespace rpc
}  // namespace sylar

#endif
