#ifndef __SYLAR_RPC_RPC_CONTROLLER_H__
#define __SYLAR_RPC_RPC_CONTROLLER_H__

#include <google/protobuf/service.h>
#include <memory>
#include <string>

namespace sylar {
namespace rpc {

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

private:
    bool m_failed;
    std::string m_errText;
};

}
}

#endif
