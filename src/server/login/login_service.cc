#include "login_service.h"

#include <cstring>
#include <random>

#include "sylar/core/log.h"
#include "sylar/rpc/rpc_controller.h"
#include "sylar/util/hash_util.h"
#include "sylar/util/json_util.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

// 注: sylar util 提供 sha1sum(无 sha256)。这里用 sha1 作密码哈希演示;
// 生产应替换为真正的 SHA256(可引入 OpenSSL EVP)。接口不变。
// 重要: sha1sum 返回原始 20 字节摘要(含非法 UTF-8), 不能直接存 proto string 字段,
// 必须先 hexstring_from_data 转成十六进制字符串。
static std::string hashPassword(const std::string& password, const std::string& salt) {
    return sylar::hexstring_from_data(sylar::sha1sum(password + salt));
}

// 随机字符串
static std::string randomString(size_t len) {
    static const char* alpha = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::string out;
    out.reserve(len);
    std::random_device rd;
    std::mt19937 g(rd());
    for(size_t i = 0; i < len; ++i) out.push_back(alpha[g() % 62]);
    return out;
}

LoginServiceImpl::LoginServiceImpl(const std::string& etcdEndpoint)
    : m_etcdEndpoint(etcdEndpoint) {
}

LoginServiceImpl::~LoginServiceImpl() {
}

std::shared_ptr<sylar::rpc::RpcChannel> LoginServiceImpl::dataChannel() {
    std::lock_guard<std::mutex> lk(m_channelMutex);
    if(!m_dataChannel) {
        m_dataChannel = std::make_shared<sylar::rpc::RpcChannel>(m_etcdEndpoint);
    }
    return m_dataChannel;
}

bool LoginServiceImpl::validateName(const std::string& name) {
    if(name.size() < 2 || name.size() > 16) return false;
    for(char c : name) {
        if(!(std::isalnum((unsigned char)c) || c == '_')) return false;
    }
    return true;
}

// ---- 注册核心 ----
uint64_t LoginServiceImpl::doRegister(const std::string& name, const std::string& password, std::string& err) {
    if(!validateName(name)) { err = "invalid name"; return 0; }
    if(password.size() < 4) { err = "password too short"; return 0; }

    std::string salt = randomString(32);
    std::string hash = hashPassword(password, salt);

    auto ch = dataChannel();
    ddt::DataService::Stub dataStub(ch.get());
    sylar::rpc::RpcController ctrl;
    CreateAccountReq req;
    req.set_name(name);
    req.set_password_hash(hash);
    req.set_salt(salt);
    CreateAccountResp resp;
    dataStub.CreateAccount(&ctrl, &req, &resp, nullptr);
    if(ctrl.Failed() || resp.result() != SUCCESS) {
        err = ctrl.Failed() ? ctrl.ErrorText() : (!resp.msg().empty() ? resp.msg() : "register fail");
        return 0;
    }
    return resp.account_id();
}

// ---- 登录核心 ----
uint64_t LoginServiceImpl::doLogin(const std::string& name, const std::string& password,
                                   std::string& token, std::string& err) {
    auto ch = dataChannel();
    ddt::DataService::Stub dataStub(ch.get());
    sylar::rpc::RpcController ctrl;
    NameReq nreq;
    nreq.set_name(name);
    AccountRow arow;
    dataStub.GetAccountByName(&ctrl, &nreq, &arow, nullptr);
    if(ctrl.Failed()) { err = ctrl.ErrorText(); return 0; }
    if(arow.result() != SUCCESS) { err = "account not found"; return 0; }

    std::string expect = hashPassword(password, arow.salt());
    if(expect != arow.password_hash()) { err = "wrong password"; return 0; }

    // 签发 token
    token = randomString(48);
    sylar::rpc::RpcController ctrl2;
    SaveTokenReq sreq;
    sreq.set_token(token);
    sreq.set_account_id(arow.account_id());
    sreq.set_ttl_sec(86400);
    ResultResp sresp;
    dataStub.SaveToken(&ctrl2, &sreq, &sresp, nullptr);
    if(ctrl2.Failed() || sresp.result() != SUCCESS) {
        err = ctrl2.Failed() ? ctrl2.ErrorText() : "save token fail";
        return 0;
    }
    return arow.account_id();
}

// ---- HTTP ----
std::string LoginServiceImpl::handleHttpLogin(const std::string& name, const std::string& password) {
    std::string token, err;
    uint64_t id = doLogin(name, password, token, err);
    Json::Value root;
    if(id == 0) {
        root["ok"] = false;
        root["msg"] = err;
    } else {
        root["ok"] = true;
        root["token"] = token;
        root["account_id"] = (Json::UInt64)id;
    }
    return sylar::JsonUtil::ToString(root);
}

std::string LoginServiceImpl::handleHttpRegister(const std::string& name, const std::string& password) {
    std::string err;
    uint64_t id = doRegister(name, password, err);
    Json::Value root;
    if(id == 0) {
        root["ok"] = false;
        root["msg"] = err;
    } else {
        root["ok"] = true;
        root["account_id"] = (Json::UInt64)id;
    }
    return sylar::JsonUtil::ToString(root);
}

// ---- RPC ----
void LoginServiceImpl::ValidateToken(::google::protobuf::RpcController*,
        const ValidateTokenReq* req, ValidateTokenResp* resp, ::google::protobuf::Closure* done) {
    auto ch = dataChannel();
    ddt::DataService::Stub dataStub(ch.get());
    sylar::rpc::RpcController ctrl;
    TokenReq treq;
    treq.set_token(req->token());
    TokenResp tresp;
    dataStub.LoadToken(&ctrl, &treq, &tresp, nullptr);
    if(ctrl.Failed()) {
        resp->set_result(FAIL);
        resp->set_msg(ctrl.ErrorText());
        if(done) done->Run();
        return;
    }
    if(tresp.result() != SUCCESS) {
        resp->set_result(AUTH_FAIL);
        if(done) done->Run();
        return;
    }
    // 取账号名
    sylar::rpc::RpcController ctrl2;
    IdReq ireq;
    ireq.set_account_id(tresp.account_id());
    AccountRow arow;
    dataStub.GetAccountById(&ctrl2, &ireq, &arow, nullptr);
    resp->set_result(SUCCESS);
    resp->set_account_id(tresp.account_id());
    resp->set_name(ctrl2.Failed() ? std::string() : arow.name());
    if(done) done->Run();
}

void LoginServiceImpl::Register(::google::protobuf::RpcController*,
        const RegisterRpcReq* req, RegisterRpcResp* resp, ::google::protobuf::Closure* done) {
    std::string err;
    uint64_t id = doRegister(req->name(), req->password(), err);
    if(id == 0) {
        resp->set_result(FAIL);
        resp->set_msg(err);
    } else {
        resp->set_result(SUCCESS);
        resp->set_account_id(id);
    }
    if(done) done->Run();
}

void LoginServiceImpl::Login(::google::protobuf::RpcController*,
        const LoginRpcReq* req, LoginRpcResp* resp, ::google::protobuf::Closure* done) {
    std::string token, err;
    uint64_t id = doLogin(req->name(), req->password(), token, err);
    if(id == 0) {
        resp->set_result(FAIL);
        resp->set_msg(err);
    } else {
        resp->set_result(SUCCESS);
        resp->set_account_id(id);
        resp->set_token(token);
    }
    if(done) done->Run();
}

} // namespace ddt
