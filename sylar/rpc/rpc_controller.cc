#include "rpc_controller.h"

namespace sylar {
namespace rpc {

RpcController::RpcController()
    : m_failed(false)
    , m_errText("") {
}

RpcController::~RpcController() {
}

void RpcController::Reset() {
    m_failed = false;
    m_errText = "";
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

void RpcController::StartCancel() {
}

bool RpcController::IsCanceled() const {
    return false;
}

void RpcController::NotifyOnCancel(google::protobuf::Closure* callback) {
}

}
}
