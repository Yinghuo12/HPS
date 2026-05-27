#include "sylar/rpc/rpc_provider.h"
#include "sylar/iomanager.h"
#include "sylar/log.h"
#include "test.pb.h"
#include <memory>

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

class TestServiceImpl : public test::TestService {
public:
    void Login(::google::protobuf::RpcController* controller,
               const ::test::LoginRequest* request,
               ::test::LoginResponse* response,
               ::google::protobuf::Closure* done) override {
        SYLAR_LOG_INFO(g_logger) << "RPC Login called: name=" << request->name()
            << " pwd=" << request->pwd();

        bool ok = (request->name() == "admin" && request->pwd() == "123");
        response->set_errcode(ok ? 0 : 1);
        response->set_errmsg(ok ? "ok" : "auth failed");
        response->set_success(ok);

        done->Run();
    }
};

int main() {
    sylar::IOManager iom(2);
    iom.schedule([]() {
        auto provider = std::make_shared<sylar::rpc::RpcProvider>();
        provider->notifyService(new TestServiceImpl());
        provider->run();
    });
    return 0;
}
