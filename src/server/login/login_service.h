#ifndef __DDT_LOGIN_SERVICE_H__
#define __DDT_LOGIN_SERVICE_H__

#include <memory>
#include <string>

#include "rpc.pb.h"
#include "sylar/rpc/rpc_channel.h"

namespace ddt {

// 注册/登录/改密 + token 签发与校验。
// 持久化全部经 DataService RPC(不直连 DB)。密码 SHA256(password+salt), token 存 Redis。
class LoginServiceImpl : public LoginService {
public:
    typedef std::shared_ptr<LoginServiceImpl> ptr;

    explicit LoginServiceImpl(const std::string& etcdEndpoint);
    ~LoginServiceImpl();

    // ---- RPC 方法 ----
    void ValidateToken(::google::protobuf::RpcController* controller,
                       const ::ddt::ValidateTokenReq* request,
                       ::ddt::ValidateTokenResp* response,
                       ::google::protobuf::Closure* done) override;
    void Register(::google::protobuf::RpcController* controller,
                  const ::ddt::RegisterRpcReq* request,
                  ::ddt::RegisterRpcResp* response,
                  ::google::protobuf::Closure* done) override;
    void Login(::google::protobuf::RpcController* controller,
               const ::ddt::LoginRpcReq* request,
               ::ddt::LoginRpcResp* response,
               ::google::protobuf::Closure* done) override;

    // ---- HTTP 处理器(供 login 服 HttpServer 调用) ----
    // 返回 JSON 字符串
    std::string handleHttpLogin(const std::string& name, const std::string& password);
    std::string handleHttpRegister(const std::string& name, const std::string& password);

    // 校验账号名合法性: 2-16 字符, [a-zA-Z0-9_]
    static bool validateName(const std::string& name);

private:
    // 内部: 完成"查账号->校验密码->签发token"的登录核心, 返回 account_id(0=失败)
    uint64_t doLogin(const std::string& name, const std::string& password, std::string& token, std::string& err);
    uint64_t doRegister(const std::string& name, const std::string& password, std::string& err);

    // 调 DataService RPC 的便捷封装
    std::shared_ptr<sylar::rpc::RpcChannel> dataChannel();

    std::string m_etcdEndpoint;
    std::mutex m_channelMutex;
    std::shared_ptr<sylar::rpc::RpcChannel> m_dataChannel;
};

} // namespace ddt

#endif
