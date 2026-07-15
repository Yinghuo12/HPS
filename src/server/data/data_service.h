#ifndef __DDT_DATA_SERVICE_H__
#define __DDT_DATA_SERVICE_H__

#include <memory>

#include "rpc.pb.h"
#include "sylar/orm/connection_pool.h"

namespace ddt {

// ============================================================
// DataService: 唯一持久化层。
// 实现 rpc.proto 的 DataService(protobuf Service)。
//
// 后端:
//   - MySQL(经 sylar ConnectionPool) 存账号/档案/战绩/好友/聊天记录
//   - Redis 存 token->accountId(会话)
//
// 所有方法为同步(在 RPC 光纤内执行, ORM 连接池天然异步等待)。
// ============================================================
class DataServiceImpl : public DataService {
public:
    typedef std::shared_ptr<DataServiceImpl> ptr;

    DataServiceImpl();
    ~DataServiceImpl();

    // 初始化连接池/Redis。失败返回 false。
    bool init(const std::string& dbHost, int dbPort, const std::string& dbUser,
              const std::string& dbPass, const std::string& dbName, int poolSize,
              const std::string& redisHost, int redisPort);

    // ---- DataService RPC 方法实现 ----
    void CreateAccount(::google::protobuf::RpcController* controller,
                       const ::ddt::CreateAccountReq* request,
                       ::ddt::CreateAccountResp* response,
                       ::google::protobuf::Closure* done) override;
    void GetAccountByName(::google::protobuf::RpcController* controller,
                          const ::ddt::NameReq* request,
                          ::ddt::AccountRow* response,
                          ::google::protobuf::Closure* done) override;
    void GetAccountById(::google::protobuf::RpcController* controller,
                        const ::ddt::IdReq* request,
                        ::ddt::AccountRow* response,
                        ::google::protobuf::Closure* done) override;
    void VerifyPassword(::google::protobuf::RpcController* controller,
                        const ::ddt::VerifyPwdReq* request,
                        ::ddt::VerifyPwdResp* response,
                        ::google::protobuf::Closure* done) override;
    void UpdateGender(::google::protobuf::RpcController* controller,
                      const ::ddt::UpdateGenderReq* request,
                      ::ddt::ResultResp* response,
                      ::google::protobuf::Closure* done) override;
    void GetProfile(::google::protobuf::RpcController* controller,
                    const ::ddt::IdReq* request,
                    ::ddt::ProfileRow* response,
                    ::google::protobuf::Closure* done) override;
    void UpdateWinLoss(::google::protobuf::RpcController* controller,
                       const ::ddt::UpdateWinLossReq* request,
                       ::ddt::ResultResp* response,
                       ::google::protobuf::Closure* done) override;
    void SaveToken(::google::protobuf::RpcController* controller,
                   const ::ddt::SaveTokenReq* request,
                   ::ddt::ResultResp* response,
                   ::google::protobuf::Closure* done) override;
    void LoadToken(::google::protobuf::RpcController* controller,
                   const ::ddt::TokenReq* request,
                   ::ddt::TokenResp* response,
                   ::google::protobuf::Closure* done) override;
    void DeleteToken(::google::protobuf::RpcController* controller,
                     const ::ddt::TokenReq* request,
                     ::ddt::ResultResp* response,
                     ::google::protobuf::Closure* done) override;
    void SaveGameRecord(::google::protobuf::RpcController* controller,
                        const ::ddt::GameRecordReq* request,
                        ::ddt::ResultResp* response,
                        ::google::protobuf::Closure* done) override;
    void AddFriend(::google::protobuf::RpcController* controller,
                   const ::ddt::AddFriendReq* request,
                   ::ddt::ResultResp* response,
                   ::google::protobuf::Closure* done) override;
    void GetFriendList(::google::protobuf::RpcController* controller,
                       const ::ddt::IdReq* request,
                       ::ddt::FriendListRpcResp* response,
                       ::google::protobuf::Closure* done) override;
    void SaveChat(::google::protobuf::RpcController* controller,
                  const ::ddt::SaveChatReq* request,
                  ::ddt::ResultResp* response,
                  ::google::protobuf::Closure* done) override;
    void GetChatHistory(::google::protobuf::RpcController* controller,
                        const ::ddt::GetChatHistoryReq* request,
                        ::ddt::ChatHistoryRespRpc* response,
                        ::google::protobuf::Closure* done) override;
    void GetPrivateHistory(::google::protobuf::RpcController* controller,
                           const ::ddt::GetPrivateHistoryReq* request,
                           ::ddt::ChatHistoryRespRpc* response,
                           ::google::protobuf::Closure* done) override;

private:
    sylar::ConnectionPool* pool() { return m_pool.get(); }

    std::shared_ptr<sylar::ConnectionPool> m_pool;
    // Redis: 简化起见, 单连接 + 互斥(后续可换连接池)
    void* m_redis;            // redisContext*
    std::mutex m_redisMutex;  // 保护 m_redis
};

} // namespace ddt

#endif
