#include "data_service.h"

#include <hiredis/hiredis.h>
#include <mysql/errmsg.h>

#include <cstring>
#include <mutex>

#include "sylar/core/log.h"
#include "sylar/orm/database.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

// SQL 转义(用 mysql 真转义, 防注入)
static std::string esc(sylar::Connection* c, const std::string& s) {
    if(!c || s.empty()) return std::string();
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long n = mysql_real_escape_string(c->getRaw(), &out[0], s.c_str(), s.size());
    out.resize(n);
    return out;
}

// ============================================================
// DataServiceImpl
// ============================================================
DataServiceImpl::DataServiceImpl()
    : m_redis(nullptr) {
}

DataServiceImpl::~DataServiceImpl() {
    if(m_redis) {
        std::lock_guard<std::mutex> lk(m_redisMutex);
        redisFree((redisContext*)m_redis);
        m_redis = nullptr;
    }
}

bool DataServiceImpl::init(const std::string& dbHost, int dbPort, const std::string& dbUser,
                           const std::string& dbPass, const std::string& dbName, int poolSize,
                           const std::string& redisHost, int redisPort) {
    m_pool.reset(new sylar::ConnectionPool());
    m_pool->size(poolSize);
    if(!m_pool->create(dbHost, dbPort, dbUser, dbPass, dbName)) {
        SYLAR_LOG_ERROR(g_logger) << "data: ConnectionPool create fail";
        return false;
    }
    m_pool->warmup(poolSize);

    // Redis
    struct timeval tv = {1, 500000};   // 1.5s
    redisContext* rc = redisConnectWithTimeout(redisHost.c_str(), redisPort, tv);
    if(!rc || rc->err) {
        SYLAR_LOG_ERROR(g_logger) << "data: redis connect fail: "
            << (rc ? rc->errstr : "null");
        if(rc) redisFree(rc);
        m_redis = nullptr;
    } else {
        m_redis = rc;
        SYLAR_LOG_INFO(g_logger) << "data: redis connected " << redisHost << ":" << redisPort;
    }
    SYLAR_LOG_INFO(g_logger) << "data: ConnectionPool ready (" << dbHost << ":" << dbPort
        << "/" << dbName << ", pool=" << poolSize << ")";
    return true;
}

// ---- Redis 辅助(锁内调用) ----
static bool redisSet(redisContext* rc, const std::string& k, const std::string& v, int ttl) {
    redisReply* r = (redisReply*)redisCommand(rc, "SET %s %s EX %d", k.c_str(), v.c_str(), ttl);
    if(!r) return false;
    bool ok = (r->type == REDIS_REPLY_STATUS && r->str && strcmp(r->str, "OK") == 0);
    freeReplyObject(r);
    return ok;
}
static std::string redisGet(redisContext* rc, const std::string& k) {
    redisReply* r = (redisReply*)redisCommand(rc, "GET %s", k.c_str());
    if(!r) return std::string();
    std::string v;
    if(r->type == REDIS_REPLY_STRING && r->str) v = r->str;
    freeReplyObject(r);
    return v;
}
static bool redisDel(redisContext* rc, const std::string& k) {
    redisReply* r = (redisReply*)redisCommand(rc, "DEL %s", k.c_str());
    if(!r) return false;
    bool ok = (r->type == REDIS_REPLY_INTEGER);
    freeReplyObject(r);
    return ok;
}

// ---- 账号 ----
void DataServiceImpl::CreateAccount(::google::protobuf::RpcController*,
        const CreateAccountReq* req, CreateAccountResp* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); resp->set_msg("db unavailable"); if(done){done->Run();} return; }
    sylar::Connection* c = db.getConnection();
    // 先查重
    auto rows = db.query("SELECT id FROM accounts WHERE name='" + esc(c, req->name()) + "' LIMIT 1");
    if(!rows.empty()) {
        resp->set_result(ALREADY); resp->set_msg("name exists");
        if(done){done->Run();} return;
    }
    std::string sql = "INSERT INTO accounts(name,password_hash,salt) VALUES('"
        + esc(c, req->name()) + "','" + esc(c, req->password_hash()) + "','" + esc(c, req->salt()) + "')";
    if(!db.execute(sql)) { resp->set_result(FAIL); resp->set_msg("insert fail"); if(done){done->Run();} return; }
    uint64_t id = (uint64_t)db.getLastInsertId();
    // 建档案
    db.execute("INSERT INTO player_profiles(account_id,nickname,level,exp,wins,losses) VALUES("
        + std::to_string(id) + ",'" + esc(c, req->name()) + "',1,0,0,0)");
    resp->set_result(SUCCESS);
    resp->set_account_id(id);
    if(done) done->Run();
}

void DataServiceImpl::GetAccountByName(::google::protobuf::RpcController*,
        const NameReq* req, AccountRow* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    sylar::Connection* c = db.getConnection();
    auto rows = db.query("SELECT id,name,password_hash,salt,COALESCE(gender,0) AS gender FROM accounts WHERE name='"
        + esc(c, req->name()) + "' LIMIT 1");
    if(rows.empty()) { resp->set_result(NOT_FOUND); if(done){done->Run();} return; }
    auto& r = rows[0];
    resp->set_result(SUCCESS);
    resp->set_account_id((uint64_t)(int64_t)r["id"]);
    resp->set_name((std::string)r["name"]);
    resp->set_password_hash((std::string)r["password_hash"]);
    resp->set_salt((std::string)r["salt"]);
    if(r.count("gender")) resp->set_gender((ddt::Gender)(int)r["gender"]);
    if(done) done->Run();
}

void DataServiceImpl::GetAccountById(::google::protobuf::RpcController*,
        const IdReq* req, AccountRow* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    auto rows = db.query("SELECT id,name,password_hash,salt,COALESCE(gender,0) AS gender FROM accounts WHERE id="
        + std::to_string(req->account_id()) + " LIMIT 1");
    if(rows.empty()) { resp->set_result(NOT_FOUND); if(done){done->Run();} return; }
    auto& r = rows[0];
    resp->set_result(SUCCESS);
    resp->set_account_id((uint64_t)(int64_t)r["id"]);
    resp->set_name((std::string)r["name"]);
    resp->set_password_hash((std::string)r["password_hash"]);
    resp->set_salt((std::string)r["salt"]);
    if(r.count("gender")) resp->set_gender((ddt::Gender)(int)r["gender"]);
    if(done) done->Run();
}

void DataServiceImpl::VerifyPassword(::google::protobuf::RpcController*,
        const VerifyPwdReq* req, VerifyPwdResp* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    auto rows = db.query("SELECT password_hash FROM accounts WHERE id="
        + std::to_string(req->account_id()) + " LIMIT 1");
    if(rows.empty()) { resp->set_result(NOT_FOUND); if(done){done->Run();} return; }
    if((std::string)rows[0]["password_hash"] == req->password_hash()) {
        resp->set_result(SUCCESS);
    } else {
        resp->set_result(AUTH_FAIL);
    }
    if(done) done->Run();
}

void DataServiceImpl::UpdateGender(::google::protobuf::RpcController*,
        const UpdateGenderReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); resp->set_msg("db unavailable"); if(done){done->Run();} return; }
    sylar::Connection* c = db.getConnection();
    // 用 try 兼容旧库(gender 列可能不存在, 第一次会失败 → 自动加列再重试)
    std::string sql = "UPDATE accounts SET gender=" + std::to_string((int)req->gender())
        + " WHERE id=" + std::to_string(req->account_id());
    if(!db.execute(sql)) {
        // 列可能不存在, 尝试 ALTER TABLE 加列后重试
        db.execute("ALTER TABLE accounts ADD COLUMN gender TINYINT DEFAULT 0");
        if(!db.execute(sql)) { resp->set_result(FAIL); resp->set_msg("update gender fail"); if(done){done->Run();} return; }
    }
    resp->set_result(SUCCESS);
    if(done) done->Run();
}

// ---- 档案 ----
void DataServiceImpl::GetProfile(::google::protobuf::RpcController*,
        const IdReq* req, ProfileRow* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    auto rows = db.query("SELECT account_id,nickname,level,wins,losses FROM player_profiles WHERE account_id="
        + std::to_string(req->account_id()) + " LIMIT 1");
    if(rows.empty()) { resp->set_result(NOT_FOUND); if(done){done->Run();} return; }
    auto& r = rows[0];
    resp->set_result(SUCCESS);
    resp->set_account_id((uint64_t)(int64_t)r["account_id"]);
    if(r["nickname"].isString()) resp->set_nickname((std::string)r["nickname"]);
    resp->set_level((int64_t)r["level"]);
    resp->set_wins((int64_t)r["wins"]);
    resp->set_losses((int64_t)r["losses"]);
    if(done) done->Run();
}

void DataServiceImpl::UpdateWinLoss(::google::protobuf::RpcController*,
        const UpdateWinLossReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    std::string col = req->win() ? "wins" : "losses";
    bool ok = db.execute("UPDATE player_profiles SET " + col + "=" + col + "+1 WHERE account_id="
        + std::to_string(req->account_id()));
    resp->set_result(ok ? SUCCESS : FAIL);
    if(done) done->Run();
}

// ---- Token (Redis) ----
void DataServiceImpl::SaveToken(::google::protobuf::RpcController*,
        const SaveTokenReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    if(!m_redis) { resp->set_result(FAIL); resp->set_msg("redis unavailable"); if(done){done->Run();} return; }
    std::lock_guard<std::mutex> lk(m_redisMutex);
    std::string k = "session:" + req->token();
    bool ok = redisSet((redisContext*)m_redis, k, std::to_string(req->account_id()), req->ttl_sec());
    resp->set_result(ok ? SUCCESS : FAIL);
    if(done) done->Run();
}
void DataServiceImpl::LoadToken(::google::protobuf::RpcController*,
        const TokenReq* req, TokenResp* resp, ::google::protobuf::Closure* done) {
    if(!m_redis) { resp->set_result(FAIL); if(done){done->Run();} return; }
    std::lock_guard<std::mutex> lk(m_redisMutex);
    std::string k = "session:" + req->token();
    std::string v = redisGet((redisContext*)m_redis, k);
    if(v.empty()) { resp->set_result(AUTH_FAIL); if(done){done->Run();} return; }
    resp->set_result(SUCCESS);
    resp->set_account_id((uint64_t)strtoll(v.c_str(), nullptr, 10));
    if(done) done->Run();
}
void DataServiceImpl::DeleteToken(::google::protobuf::RpcController*,
        const TokenReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    if(!m_redis) { resp->set_result(FAIL); if(done){done->Run();} return; }
    std::lock_guard<std::mutex> lk(m_redisMutex);
    std::string k = "session:" + req->token();
    redisDel((redisContext*)m_redis, k);
    resp->set_result(SUCCESS);
    if(done) done->Run();
}

// ---- 战绩 ----
void DataServiceImpl::SaveGameRecord(::google::protobuf::RpcController*,
        const GameRecordReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    // 兼容旧表结构: 取 player_ids 前两个 + winner_ids 第一个
    uint64_t p1 = req->player_ids_size() > 0 ? req->player_ids(0) : 0;
    uint64_t p2 = req->player_ids_size() > 1 ? req->player_ids(1) : 0;
    uint64_t w  = req->winner_ids_size() > 0 ? req->winner_ids(0) : 0;
    std::string sql = "INSERT INTO game_records(player1_id,player2_id,winner_id,duration) VALUES("
        + std::to_string(p1) + "," + std::to_string(p2) + ","
        + std::to_string(w) + "," + std::to_string(req->duration()) + ")";
    resp->set_result(db.execute(sql) ? SUCCESS : FAIL);
    if(done) done->Run();
}

// ---- 好友 ----
void DataServiceImpl::AddFriend(::google::protobuf::RpcController*,
        const AddFriendReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    // 双向插入
    std::string sql = "INSERT IGNORE INTO friends(account_id,friend_id) VALUES("
        + std::to_string(req->account_id()) + "," + std::to_string(req->friend_id()) + "),("
        + std::to_string(req->friend_id()) + "," + std::to_string(req->account_id()) + ")";
    resp->set_result(db.execute(sql) ? SUCCESS : FAIL);
    if(done) done->Run();
}

void DataServiceImpl::GetFriendList(::google::protobuf::RpcController*,
        const IdReq* req, FriendListRpcResp* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    auto rows = db.query("SELECT a.id AS fid,a.name AS fname,p.level AS level FROM friends f "
        "JOIN accounts a ON f.friend_id=a.id "
        "LEFT JOIN player_profiles p ON p.account_id=a.id "
        "WHERE f.account_id=" + std::to_string(req->account_id()));
    resp->set_result(SUCCESS);
    for(auto& r : rows) {
        auto* f = resp->add_friends();
        f->set_account_id((uint64_t)(int64_t)r["fid"]);
        f->set_name((std::string)r["fname"]);
        f->set_level((!r["level"].isNull() ? (int32_t)(int64_t)r["level"] : 1));
        f->set_online(false);   // 在线状态由 login 服查询(此处不查 Redis online:*)
    }
    if(done) done->Run();
}

// ---- 聊天记录 ----
void DataServiceImpl::SaveChat(::google::protobuf::RpcController*,
        const SaveChatReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    sylar::Connection* c = db.getConnection();
    std::string sql = "INSERT INTO chat_history(channel,sender_id,sender_name,message,target_id) VALUES("
        + std::to_string((int)req->channel()) + "," + std::to_string(req->sender_id()) + ",'"
        + esc(c, req->sender_name()) + "','" + esc(c, req->message()) + "',"
        + std::to_string(req->target_id()) + ")";
    resp->set_result(db.execute(sql) ? SUCCESS : FAIL);
    if(done) done->Run();
}

void DataServiceImpl::GetChatHistory(::google::protobuf::RpcController*,
        const GetChatHistoryReq* req, ChatHistoryRespRpc* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    int cnt = req->count() > 0 ? req->count() : 50;
    auto rows = db.query("SELECT sender_id,sender_name,message,UNIX_TIMESTAMP(created_at) AS ts FROM chat_history "
        "WHERE channel=" + std::to_string((int)req->channel()) + " ORDER BY id DESC LIMIT " + std::to_string(cnt));
    resp->set_result(SUCCESS);
    for(auto it = rows.rbegin(); it != rows.rend(); ++it) {   // 倒序取后正序填
        auto* e = resp->add_entries();
        e->set_sender_id((uint64_t)(int64_t)(*it)["sender_id"]);
        e->set_sender_name((std::string)(*it)["sender_name"]);
        e->set_message((std::string)(*it)["message"]);
        e->set_timestamp((uint64_t)(int64_t)(*it)["ts"]);
    }
    if(done) done->Run();
}

void DataServiceImpl::GetPrivateHistory(::google::protobuf::RpcController*,
        const GetPrivateHistoryReq* req, ChatHistoryRespRpc* resp, ::google::protobuf::Closure* done) {
    sylar::Database db(m_pool.get());
    if(!db.valid()) { resp->set_result(FAIL); if(done){done->Run();} return; }
    int cnt = req->count() > 0 ? req->count() : 50;
    auto rows = db.query("SELECT sender_id,sender_name,message,UNIX_TIMESTAMP(created_at) AS ts FROM chat_history "
        "WHERE channel=" + std::to_string((int)CHANNEL_PRIVATE) + " "
        "AND ((sender_id=" + std::to_string(req->my_id()) + " AND target_id=" + std::to_string(req->target_id()) + ") "
        "OR (sender_id=" + std::to_string(req->target_id()) + " AND target_id=" + std::to_string(req->my_id()) + ")) "
        "ORDER BY id DESC LIMIT " + std::to_string(cnt));
    resp->set_result(SUCCESS);
    for(auto it = rows.rbegin(); it != rows.rend(); ++it) {
        auto* e = resp->add_entries();
        e->set_sender_id((uint64_t)(int64_t)(*it)["sender_id"]);
        e->set_sender_name((std::string)(*it)["sender_name"]);
        e->set_message((std::string)(*it)["message"]);
        e->set_timestamp((uint64_t)(int64_t)(*it)["ts"]);
    }
    if(done) done->Run();
}

} // namespace ddt
