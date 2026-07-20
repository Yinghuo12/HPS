// 战绩多人落库冒烟测试

#include <cstdio>
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

int main() {
    sylar::IOManager iom(2, true, "rec-smoke");
    iom.schedule([]() {
        SYLAR_LOG_INFO(g_logger) << "==== save record smoke START ====";

        // 4 玩家战绩(2v2, 红队全胜)。占位账号 90001~90004 由 shell wrapper 预置
        ddt::DataService::Stub stub(new sylar::rpc::RpcChannel(kEtcd));
        ddt::GameRecordReq req;
        req.set_duration(180);
        req.set_winning_team(ddt::TEAM_RED);
        auto* p1 = req.add_players();
        p1->set_account_id(90001);
        p1->set_team(ddt::TEAM_RED);
        p1->set_is_winner(true);
        p1->set_damage_dealt(125);
        auto* p2 = req.add_players();
        p2->set_account_id(90002);
        p2->set_team(ddt::TEAM_RED);
        p2->set_is_winner(true);
        p2->set_damage_dealt(80);
        auto* p3 = req.add_players();
        p3->set_account_id(90003);
        p3->set_team(ddt::TEAM_BLUE);
        p3->set_is_winner(false);
        p3->set_damage_dealt(60);
        auto* p4 = req.add_players();
        p4->set_account_id(90004);
        p4->set_team(ddt::TEAM_BLUE);
        p4->set_is_winner(false);
        p4->set_damage_dealt(40);

        ddt::ResultResp resp;
        sylar::rpc::RpcController ctrl;
        stub.SaveGameRecord(&ctrl, &req, &resp, nullptr);
        if (ctrl.Failed()) {
            ++g_fail;
            SYLAR_LOG_ERROR(g_logger) << "[FAIL] SaveGameRecord RPC: " << ctrl.ErrorText();
        } else if (resp.result() != ddt::SUCCESS) {
            ++g_fail;
            SYLAR_LOG_ERROR(g_logger) << "[FAIL] SaveGameRecord result="
                << resp.result() << " msg=" << resp.msg();
        } else {
            ++g_pass;
            SYLAR_LOG_INFO(g_logger) << "[PASS] SaveGameRecord 4-player record saved (result=SUCCESS)";
        }

        SYLAR_LOG_INFO(g_logger) << "==== save record smoke RESULT: " << g_pass << " passed, " << g_fail << " failed ====";
        std::cout.flush();
        std::fflush(stdout);
        std::fflush(stderr);
        std::fprintf(stderr, ">>> save record result: %d passed, %d failed (exit %d)\n",
                     g_pass, g_fail, g_fail ? 1 : 0);
        std::_Exit(g_fail ? 1 : 0);
    });
    return 0;
}
