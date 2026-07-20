// RPC Controller：承载调用状态（失败/错误/traceId），实现 google::protobuf::RpcController 接口。
#include "rpc_controller.h"

namespace sylar {
namespace rpc {

RpcController::RpcController()
    : m_failed(false)
    , m_errText("") {
}

RpcController::~RpcController() {
}

// 复位状态：清空失败标志与 traceId。
void RpcController::Reset() {
    m_failed = false;
    m_errText = "";
    m_traceId.clear();
}

bool RpcController::Failed() const {
    return m_failed;
}

std::string RpcController::ErrorText() const {
    return m_errText;
}

void RpcController::SetFailed(const std::string& reason) {
    m_failed = true;
    m_errText = reason;
}

// 取消相关接口本实现未启用，保留为空以满足 protobuf 接口契约。
void RpcController::StartCancel() {
}

bool RpcController::IsCanceled() const {
    return false;
}

void RpcController::NotifyOnCancel(google::protobuf::Closure* callback) {
}

}   // namespace rpc
}   // namespace sylar
