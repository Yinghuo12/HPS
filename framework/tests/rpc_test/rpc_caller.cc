#include "sylar/rpc/rpc_channel.h"
#include "sylar/rpc/rpc_controller.h"
#include "sylar/iomanager.h"
#include "sylar/log.h"
#include "test.pb.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

int main() {
    sylar::IOManager iom(2);
    iom.schedule([]() {
        test::TestService_Stub stub(new sylar::rpc::RpcChannel());

        test::LoginRequest request;
        request.set_name("admin");
        request.set_pwd("123");

        test::LoginResponse response;
        sylar::rpc::RpcController controller;

        stub.Login(&controller, &request, &response, nullptr);

        if(controller.Failed()) {
            SYLAR_LOG_ERROR(g_logger) << "RPC failed: " << controller.ErrorText();
        } else {
            SYLAR_LOG_INFO(g_logger) << "RPC response: success=" << response.success()
                << " errcode=" << response.errcode()
                << " errmsg=" << response.errmsg();
        }
    });
    return 0;
}
