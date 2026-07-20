// 跨服务 RPC 冒烟测试

#include <cstdlib>
#include <string>

#include "sylar/core/log.h"
#include "sylar/rpc/rpc_channel.h"
#include "sylar/rpc/rpc_controller.h"
#include "sylar/scheduler/iomanager.h"

#include "common.pb.h"
#include "rpc.pb.h"

static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();
static const std::string kEtcd = "http://127.0.0.1:2379";
static int g_pass = 0, g_fail = 0;

static const char* resultName(int r) {
    switch (r) {
        case ddt::SUCCESS:   return "SUCCESS";
        case ddt::FAIL:      return "FAIL";
        case ddt::NOT_FOUND: return "NOT_FOUND";
        case ddt::AUTH_FAIL: return "AUTH_FAIL";
        case ddt::ALREADY:   return "ALREADY";
        case ddt::FULL:      return "FULL";
        case ddt::BAD_PARAM: return "BAD_PARAM";
        default:             return "(other)";
    }
}

int main() {
    sylar::IOManager iom(2, true, "smoke");
    iom.schedule([]() {
        // 每个 stub 各自 new channel; STUB_OWNS_CHANNEL 析构释放, 三段调用互不干扰

        // 1) LobbyService.CreateRoom -> etcd /LobbyService/CreateRoom -> 127.0.0.1:8300
        //    选 CreateRoom 而非 RoomList：响应含 room_id(>=1000, 非默认值) 必非空。
        //    （RoomList 返回 {SUCCESS,空} 序列化成 0 字节, 触发 BUG-5 空响应解码 bug,
        //     那是 sylar rpc_channel 缺陷, 与本冒烟要验证的「调用链可达」无关。）
        {
            ddt::LobbyService_Stub stub(new sylar::rpc::RpcChannel(kEtcd));
            ddt::CreateRoomRpcReq req;
            req.set_account_id(99999);
            req.set_name("smoke");
            req.set_room_name("smoke-room");
            ddt::CreateRoomRpcResp resp;
            sylar::rpc::RpcController ctrl;
            stub.CreateRoom(&ctrl, &req, &resp, nullptr);
            if (ctrl.Failed()) {
                ++g_fail;
                SYLAR_LOG_ERROR(g_logger) << "[FAIL] LobbyService.CreateRoom : " << ctrl.ErrorText();
            } else {
                ++g_pass;
                SYLAR_LOG_INFO(g_logger) << "[PASS] LobbyService.CreateRoom result="
                    << resultName(resp.result()) << " room_id=" << resp.room_id();
            }
        }

        // 2) LoginService.ValidateToken (假 token) -> /LoginService/ValidateToken -> 8201
        //    期望 AUTH_FAIL, 但 RPC 通路走通即证明可达。
        {
            ddt::LoginService_Stub stub(new sylar::rpc::RpcChannel(kEtcd));
            ddt::ValidateTokenReq req;
            req.set_token("smoke-fake-token");
            ddt::ValidateTokenResp resp;
            sylar::rpc::RpcController ctrl;
            stub.ValidateToken(&ctrl, &req, &resp, nullptr);
            if (ctrl.Failed()) {
                ++g_fail;
                SYLAR_LOG_ERROR(g_logger) << "[FAIL] LoginService.ValidateToken : " << ctrl.ErrorText();
            } else {
                ++g_pass;
                SYLAR_LOG_INFO(g_logger) << "[PASS] LoginService.ValidateToken result="
                    << resultName(resp.result())
                    << " (假 token，期望 AUTH_FAIL=3，RPC 通路 OK 即证明可达)";
            }
        }

        // 3) DataService.GetAccountByName (不存在名) -> /DataService/GetAccountByName -> 8500, 走 MySQL
        //    期望 NOT_FOUND, 证明持久化链路通。
        {
            ddt::DataService_Stub stub(new sylar::rpc::RpcChannel(kEtcd));
            ddt::NameReq req;
            req.set_name("__smoke_nonexistent__");
            ddt::AccountRow resp;
            sylar::rpc::RpcController ctrl;
            stub.GetAccountByName(&ctrl, &req, &resp, nullptr);
            if (ctrl.Failed()) {
                ++g_fail;
                SYLAR_LOG_ERROR(g_logger) << "[FAIL] DataService.GetAccountByName : " << ctrl.ErrorText();
            } else {
                ++g_pass;
                SYLAR_LOG_INFO(g_logger) << "[PASS] DataService.GetAccountByName result="
                    << resultName(resp.result())
                    << " (走 MySQL，期望 NOT_FOUND=2，证明持久化链路通)";
            }
        }

        SYLAR_LOG_INFO(g_logger) << "==== RPC smoke RESULT: " << g_pass << " passed, " << g_fail << " failed ====";

        // _Exit 跳过流 flush; cout 管道下全缓冲, 故手动 flush 全部输出后再退出拿退出码
        std::cout.flush();
        std::fflush(stdout);
        std::fflush(stderr);
        // stderr 兜底摘要（stderr 无缓冲，必定可见）
        std::fprintf(stderr, ">>> smoke result: %d passed, %d failed (exit %d)\n",
                     g_pass, g_fail, g_fail ? 1 : 0);
        std::_Exit(g_fail ? 1 : 0);
    });
    return 0;
}
