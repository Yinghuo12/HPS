#include "data_service.h"

#include <hiredis/hiredis.h>
#include <mysql/errmsg.h>

#include <cstring>
#include <set>

#include "gate.pb.h"   // ChatNotify (PublishWorldChat 构造推送消息)
#include "sylar/core/log.h"
#include "sylar/orm/database.h"
#include "sylar/orm/transaction.h"

namespace ddt {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("rpc");

// SQL 转义(用 mysql 真转义, 防注入)
// 保留: 无 DbWorkerPool 回退路径 + Transaction 内批量 SQL 仍需要。
static std::string esc(sylar::Connection* c, const std::string& s) {
    if(!c || s.empty()) return std::string();
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long n = mysql_real_escape_string(c->getRaw(), &out[0], s.c_str(), s.size());
    out.resize(n);
    return out;
}

// ---- DataServiceImpl ----
DataServiceImpl::DataServiceImpl() = default;

DataServiceImpl::~DataServiceImpl() {
    // RedisPool / ConnectionPool 通过 shared_ptr 自动析构, 此处无需手动释放。
}

bool DataServiceImpl::init(const std::string& dbHost, int dbPort, const std::string& dbUser,
                           const std::string& dbPass, const std::string& dbName, int poolSize,
                           const std::string& redisHost, int redisPort, int redisPoolSize) {
    m_pool.reset(new sylar::ConnectionPool());
    m_pool->size(poolSize);
    if(!m_pool->create(dbHost, dbPort, dbUser, dbPass, dbName)) {
        SYLAR_LOG_ERROR(g_logger) << "data: ConnectionPool create fail";
        return false;
    }
    m_pool->warmup(poolSize);

    // §10: Redis 连接池(替代单连接 + 全局 mutex), token 操作并行化。
    m_redisPool = std::make_shared<RedisPool>();
    m_redisPool->create(redisHost, redisPort, redisPoolSize);
    // 立即建首条验证可达(失败仅警告, 不阻塞启动——后续 get 会持续重试)
    {
        RedisGuard g(m_redisPool.get());
        if(!g) {
            SYLAR_LOG_ERROR(g_logger) << "data: redis first connect fail: "
                << redisHost << ":" << redisPort;
        } else {
            SYLAR_LOG_INFO(g_logger) << "data: redis pool ready " << redisHost << ":"
                << redisPort << " pool_size=" << redisPoolSize;
        }
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
    // 无 DbWorkerPool: 回退同步路径(兼容 test_rpc_smoke)
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            resp->set_msg("db unavailable");
            if(done) done->Run();
            return;
        }
        sylar::Connection* c = db.getConnection();
        // 先查重
        auto rows = db.query("SELECT id FROM accounts WHERE name='" + esc(c, req->name()) + "' LIMIT 1");
        if(!rows.empty()) {
            resp->set_result(ALREADY);
            resp->set_msg("name exists");
            if(done) done->Run();
            return;
        }
        std::string sql = "INSERT INTO accounts(name,password_hash,salt) VALUES('"
            + esc(c, req->name()) + "','" + esc(c, req->password_hash()) + "','" + esc(c, req->salt()) + "')";
        if(!db.execute(sql)) {
            resp->set_result(FAIL);
            resp->set_msg("insert fail");
            if(done) done->Run();
            return;
        }
        uint64_t id = (uint64_t)db.getLastInsertId();
        db.execute("INSERT INTO player_profiles(account_id,nickname,level,exp,wins,losses) VALUES("
            + std::to_string(id) + ",'" + esc(c, req->name()) + "',1,0,0,0)");
        resp->set_result(SUCCESS);
        resp->set_account_id(id);
        if(done) done->Run();
        return;
    }

    // 双通道: 走 query 通道(dbTask 在 DB 线程, onComplete 在协程调 done->Run)
    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                resp->set_msg("db unavailable");
                return;
            }
            sylar::Connection* c = db.getConnection();
            // 查重(预处理)
            auto stmt = c->prepare("SELECT id FROM accounts WHERE name=? LIMIT 1");
            if(!stmt) {
                resp->set_result(FAIL);
                resp->set_msg("prepare fail");
                return;
            }
            stmt->bindString(1, reqCopy.name());
            auto rows = stmt->queryRows();
            if(!rows.empty()) {
                resp->set_result(ALREADY);
                resp->set_msg("name exists");
                return;
            }
            // INSERT(预处理)
            auto ins = c->prepare("INSERT INTO accounts(name,password_hash,salt) VALUES(?,?,?)");
            if(!ins || ins->bindString(1, reqCopy.name())
                     || ins->bindString(2, reqCopy.password_hash())
                     || ins->bindString(3, reqCopy.salt())
                     || ins->execute() != 0) {
                resp->set_result(FAIL);
                resp->set_msg("insert fail");
                return;
            }
            uint64_t id = (uint64_t)c->getLastInsertId();
            // 建档案(预处理)
            auto prof = c->prepare("INSERT INTO player_profiles(account_id,nickname,level,exp,wins,losses) VALUES(?,?,1,0,0,0)");
            if(prof) {
                prof->bindUint64(1, id);
                prof->bindString(2, reqCopy.name());
                prof->execute();
            }
            resp->set_result(SUCCESS);
            resp->set_account_id(id);
        },
        [done]() {
            if(done) done->Run();
        }
    );
}

void DataServiceImpl::GetAccountByName(::google::protobuf::RpcController*,
        const NameReq* req, AccountRow* resp, ::google::protobuf::Closure* done) {
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            if(done) done->Run();
            return;
        }
        sylar::Connection* c = db.getConnection();
        auto rows = db.query("SELECT id,name,password_hash,salt,COALESCE(gender,0) AS gender FROM accounts WHERE name='"
            + esc(c, req->name()) + "' LIMIT 1");
        if(rows.empty()) {
            resp->set_result(NOT_FOUND);
            if(done) done->Run();
            return;
        }
        auto& r = rows[0];
        resp->set_result(SUCCESS);
        resp->set_account_id((uint64_t)(int64_t)r["id"]);
        resp->set_name((std::string)r["name"]);
        resp->set_password_hash((std::string)r["password_hash"]);
        resp->set_salt((std::string)r["salt"]);
        if(r.count("gender")) resp->set_gender((ddt::Gender)(int)r["gender"]);
        if(done) done->Run();
        return;
    }

    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                return;
            }
            sylar::Connection* c = db.getConnection();
            auto stmt = c->prepare("SELECT id,name,password_hash,salt,COALESCE(gender,0) AS gender FROM accounts WHERE name=? LIMIT 1");
            if(!stmt) {
                resp->set_result(FAIL);
                return;
            }
            stmt->bindString(1, reqCopy.name());
            auto rows = stmt->queryRows();
            if(rows.empty()) {
                resp->set_result(NOT_FOUND);
                return;
            }
            auto& r = rows[0];
            resp->set_result(SUCCESS);
            resp->set_account_id((uint64_t)(int64_t)r["id"]);
            resp->set_name((std::string)r["name"]);
            resp->set_password_hash((std::string)r["password_hash"]);
            resp->set_salt((std::string)r["salt"]);
            if(r.count("gender")) resp->set_gender((ddt::Gender)(int)r["gender"]);
        },
        [done]() {
            if(done) done->Run();
        }
    );
}

void DataServiceImpl::GetAccountById(::google::protobuf::RpcController*,
        const IdReq* req, AccountRow* resp, ::google::protobuf::Closure* done) {
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            if(done) done->Run();
            return;
        }
        auto rows = db.query("SELECT id,name,password_hash,salt,COALESCE(gender,0) AS gender FROM accounts WHERE id="
            + std::to_string(req->account_id()) + " LIMIT 1");
        if(rows.empty()) {
            resp->set_result(NOT_FOUND);
            if(done) done->Run();
            return;
        }
        auto& r = rows[0];
        resp->set_result(SUCCESS);
        resp->set_account_id((uint64_t)(int64_t)r["id"]);
        resp->set_name((std::string)r["name"]);
        resp->set_password_hash((std::string)r["password_hash"]);
        resp->set_salt((std::string)r["salt"]);
        if(r.count("gender")) resp->set_gender((ddt::Gender)(int)r["gender"]);
        if(done) done->Run();
        return;
    }

    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                return;
            }
            sylar::Connection* c = db.getConnection();
            auto stmt = c->prepare("SELECT id,name,password_hash,salt,COALESCE(gender,0) AS gender FROM accounts WHERE id=? LIMIT 1");
            if(!stmt) {
                resp->set_result(FAIL);
                return;
            }
            stmt->bindUint64(1, reqCopy.account_id());
            auto rows = stmt->queryRows();
            if(rows.empty()) {
                resp->set_result(NOT_FOUND);
                return;
            }
            auto& r = rows[0];
            resp->set_result(SUCCESS);
            resp->set_account_id((uint64_t)(int64_t)r["id"]);
            resp->set_name((std::string)r["name"]);
            resp->set_password_hash((std::string)r["password_hash"]);
            resp->set_salt((std::string)r["salt"]);
            if(r.count("gender")) resp->set_gender((ddt::Gender)(int)r["gender"]);
        },
        [done]() {
            if(done) done->Run();
        }
    );
}

void DataServiceImpl::VerifyPassword(::google::protobuf::RpcController*,
        const VerifyPwdReq* req, VerifyPwdResp* resp, ::google::protobuf::Closure* done) {
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            if(done) done->Run();
            return;
        }
        auto rows = db.query("SELECT password_hash FROM accounts WHERE id="
            + std::to_string(req->account_id()) + " LIMIT 1");
        if(rows.empty()) {
            resp->set_result(NOT_FOUND);
            if(done) done->Run();
            return;
        }
        if((std::string)rows[0]["password_hash"] == req->password_hash()) {
            resp->set_result(SUCCESS);
        } else {
            resp->set_result(AUTH_FAIL);
        }
        if(done) done->Run();
        return;
    }

    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                return;
            }
            sylar::Connection* c = db.getConnection();
            auto stmt = c->prepare("SELECT password_hash FROM accounts WHERE id=? LIMIT 1");
            if(!stmt) {
                resp->set_result(FAIL);
                return;
            }
            stmt->bindUint64(1, reqCopy.account_id());
            auto rows = stmt->queryRows();
            if(rows.empty()) {
                resp->set_result(NOT_FOUND);
                return;
            }
            if((std::string)rows[0]["password_hash"] == reqCopy.password_hash()) {
                resp->set_result(SUCCESS);
            } else {
                resp->set_result(AUTH_FAIL);
            }
        },
        [done]() {
            if(done) done->Run();
        }
    );
}

void DataServiceImpl::UpdateGender(::google::protobuf::RpcController*,
        const UpdateGenderReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            resp->set_msg("db unavailable");
            if(done) done->Run();
            return;
        }
        // 用 try 兼容旧库(gender 列可能不存在, 第一次会失败 → 自动加列再重试)
        std::string sql = "UPDATE accounts SET gender=" + std::to_string((int)req->gender())
            + " WHERE id=" + std::to_string(req->account_id());
        if(!db.execute(sql)) {
            // 列可能不存在, 尝试 ALTER TABLE 加列后重试
            db.execute("ALTER TABLE accounts ADD COLUMN gender TINYINT DEFAULT 0");
            if(!db.execute(sql)) {
                resp->set_result(FAIL);
                resp->set_msg("update gender fail");
                if(done) done->Run();
                return;
            }
        }
        resp->set_result(SUCCESS);
        if(done) done->Run();
        return;
    }

    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                resp->set_msg("db unavailable");
                return;
            }
            sylar::Connection* c = db.getConnection();
            // 用 try 兼容旧库(gender 列可能不存在)
            auto stmt = c->prepare("UPDATE accounts SET gender=? WHERE id=?");
            if(!stmt
                || stmt->bindInt32(1, (int32_t)reqCopy.gender())
                || stmt->bindUint64(2, reqCopy.account_id())
                || stmt->execute() != 0) {
                // 列可能不存在, 尝试 ALTER TABLE 加列后重试
                c->execute("ALTER TABLE accounts ADD COLUMN gender TINYINT DEFAULT 0");
                auto stmt2 = c->prepare("UPDATE accounts SET gender=? WHERE id=?");
                if(!stmt2
                    || stmt2->bindInt32(1, (int32_t)reqCopy.gender())
                    || stmt2->bindUint64(2, reqCopy.account_id())
                    || stmt2->execute() != 0) {
                    resp->set_result(FAIL);
                    resp->set_msg("update gender fail");
                    return;
                }
            }
            resp->set_result(SUCCESS);
        },
        [done]() {
            if(done) done->Run();
        }
    );
}

// ---- 档案 ----
void DataServiceImpl::GetProfile(::google::protobuf::RpcController*,
        const IdReq* req, ProfileRow* resp, ::google::protobuf::Closure* done) {
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            if(done) done->Run();
            return;
        }
        auto rows = db.query("SELECT account_id,nickname,level,wins,losses FROM player_profiles WHERE account_id="
            + std::to_string(req->account_id()) + " LIMIT 1");
        if(rows.empty()) {
            resp->set_result(NOT_FOUND);
            if(done) done->Run();
            return;
        }
        auto& r = rows[0];
        resp->set_result(SUCCESS);
        resp->set_account_id((uint64_t)(int64_t)r["account_id"]);
        if(r["nickname"].isString()) resp->set_nickname((std::string)r["nickname"]);
        resp->set_level((int64_t)r["level"]);
        resp->set_wins((int64_t)r["wins"]);
        resp->set_losses((int64_t)r["losses"]);
        if(done) done->Run();
        return;
    }

    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                return;
            }
            sylar::Connection* c = db.getConnection();
            auto stmt = c->prepare("SELECT account_id,nickname,level,wins,losses FROM player_profiles WHERE account_id=? LIMIT 1");
            if(!stmt) {
                resp->set_result(FAIL);
                return;
            }
            stmt->bindUint64(1, reqCopy.account_id());
            auto rows = stmt->queryRows();
            if(rows.empty()) {
                resp->set_result(NOT_FOUND);
                return;
            }
            auto& r = rows[0];
            resp->set_result(SUCCESS);
            resp->set_account_id((uint64_t)(int64_t)r["account_id"]);
            if(r["nickname"].isString()) resp->set_nickname((std::string)r["nickname"]);
            resp->set_level((int64_t)r["level"]);
            resp->set_wins((int64_t)r["wins"]);
            resp->set_losses((int64_t)r["losses"]);
        },
        [done]() {
            if(done) done->Run();
        }
    );
}

void DataServiceImpl::UpdateWinLoss(::google::protobuf::RpcController*,
        const UpdateWinLossReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            if(done) done->Run();
            return;
        }
        std::string col = req->win() ? "wins" : "losses";
        bool ok = db.execute("UPDATE player_profiles SET " + col + "=" + col + "+1 WHERE account_id="
            + std::to_string(req->account_id()));
        resp->set_result(ok ? SUCCESS : FAIL);
        if(done) done->Run();
        return;
    }

    // win 决定列名, 列名是白名单(wins/losses), 不能直接参数化列名
    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                return;
            }
            sylar::Connection* c = db.getConnection();
            std::string col = reqCopy.win() ? "wins" : "losses";
            // 列名为白名单常量, 拼接安全
            auto stmt = c->prepare("UPDATE player_profiles SET " + col + "=" + col + "+1 WHERE account_id=?");
            if(!stmt) {
                resp->set_result(FAIL);
                return;
            }
            stmt->bindUint64(1, reqCopy.account_id());
            bool ok = (stmt->execute() == 0);
            resp->set_result(ok ? SUCCESS : FAIL);
        },
        [done]() {
            if(done) done->Run();
        }
    );
}

// ---- Token (Redis) ----
// §10: 经 RedisPool 借连接, token 操作并行化(原方案单连接+全局 mutex 串行)。
//      命令失败时标 markUnhealthy 让池销毁坏连接, 下次 get 自动重建。
void DataServiceImpl::SaveToken(::google::protobuf::RpcController*,
        const SaveTokenReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    // Redis 操作留在 RPC 协程同步执行: hiredis 阻塞 IO 在协程线程被 sylar hook
    // 正确异步化(addEvent + YieldToHold)。若投到 DB 线程(std::thread), hook 的异步
    // 路径因 IOManager::GetThis()==nullptr 失效, 非阻塞 fd 的 read 直接 EAGAIN。
    if(!m_redisPool) {
        resp->set_result(FAIL);
        resp->set_msg("redis unavailable");
        if(done) done->Run();
        return;
    }
    RedisGuard g(m_redisPool.get());
    if(!g) {
        resp->set_result(FAIL);
        resp->set_msg("redis pool empty");
        if(done) done->Run();
        return;
    }
    std::string k = "session:" + req->token();
    bool ok = redisSet(g.get(), k, std::to_string(req->account_id()), req->ttl_sec());
    if(!ok || g.get()->err) g.markUnhealthy();
    resp->set_result(ok ? SUCCESS : FAIL);
    if(done) done->Run();
}

void DataServiceImpl::LoadToken(::google::protobuf::RpcController*,
        const TokenReq* req, TokenResp* resp, ::google::protobuf::Closure* done) {
    // Redis 操作留协程同步(同 SaveToken: DB 线程下 hook 失效致 EAGAIN)
    if(!m_redisPool) {
        resp->set_result(FAIL);
        if(done) done->Run();
        return;
    }
    RedisGuard g(m_redisPool.get());
    if(!g) {
        resp->set_result(FAIL);
        if(done) done->Run();
        return;
    }
    std::string k = "session:" + req->token();
    std::string v = redisGet(g.get(), k);
    if(g.get()->err) g.markUnhealthy();
    if(v.empty()) {
        resp->set_result(AUTH_FAIL);
        if(done) done->Run();
        return;
    }
    resp->set_result(SUCCESS);
    resp->set_account_id((uint64_t)strtoll(v.c_str(), nullptr, 10));
    if(done) done->Run();
}

void DataServiceImpl::DeleteToken(::google::protobuf::RpcController*,
        const TokenReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    // Redis 操作留协程同步(同 SaveToken: DB 线程下 hook 失效致 EAGAIN)
    if(!m_redisPool) {
        resp->set_result(FAIL);
        if(done) done->Run();
        return;
    }
    RedisGuard g(m_redisPool.get());
    if(!g) {
        resp->set_result(FAIL);
        resp->set_msg("redis pool empty");
        if(done) done->Run();
        return;
    }
    std::string k = "session:" + req->token();
    redisDel(g.get(), k);
    if(g.get()->err) g.markUnhealthy();
    resp->set_result(SUCCESS);
    if(done) done->Run();
}

// ---- 战绩 ----
// §13: 拆主子表(游戏记录 → 主表 + 玩家统计子表), 支持任意人数。
//   - 新路径: req->players 非空, 用主子表 + 事务保证原子性。
//   - 旧路径: req->players 空, 兼容旧 caller(取 player_ids 前 2 + winner_ids 第 1),
//             映射到新表结构(红蓝各 1, damage_dealt=0)。
//   - 事务: BEGIN → INSERT 主表 → 取 lastInsertId → 批量 INSERT 子表 → COMMIT。
//     失败任一步 rollback, RAII 也保证 scope 退出时回滚(Transaction 析构)。
//
// 注: 事务内的批量 INSERT 涉及变长 VALUES 子句, 难以用单条 PreparedStmt 参数化;
//     所有字段均为整数(无注入风险), 仍用 esc() 不需要——直接 to_string 拼接。
void DataServiceImpl::SaveGameRecord(::google::protobuf::RpcController*,
        const GameRecordReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            if(done) done->Run();
            return;
        }

        bool useNew = req->players_size() > 0;
        if(!useNew && req->player_ids_size() == 0) {
            resp->set_result(BAD_PARAM);
            resp->set_msg("empty record");
            if(done) done->Run();
            return;
        }

        // 事务绑定到本 db 借出的连接, 须与 db 同 scope 保证连接不归还中途。
        sylar::Transaction trx(db.getConnection());
        if(!trx.begin()) {
            resp->set_result(FAIL);
            resp->set_msg("begin tx fail");
            if(done) done->Run();
            return;
        }

        // 1) 主表: game_records
        std::string mainSql = "INSERT INTO game_records(winning_team,duration) VALUES("
            + std::to_string((int)req->winning_team()) + "," + std::to_string(req->duration()) + ")";
        if(trx.execute(mainSql) != 0) {
            trx.rollback();
            resp->set_result(FAIL);
            resp->set_msg("insert main fail");
            if(done) done->Run();
            return;
        }
        uint64_t rid = (uint64_t)trx.getLastInsertId();

        // 2) 子表: game_record_players(批量 VALUES 拼 SQL, 一次 INSERT)
        std::string sql = "INSERT INTO game_record_players(record_id,account_id,team,is_winner,damage_dealt) VALUES";
        if(useNew) {
            for(int i = 0; i < req->players_size(); ++i) {
                const auto& p = req->players(i);
                if(i > 0) sql += ",";
                sql += "(" + std::to_string(rid)
                     + "," + std::to_string(p.account_id())
                     + "," + std::to_string((int)p.team())
                     + "," + std::to_string(p.is_winner() ? 1 : 0)
                     + "," + std::to_string(p.damage_dealt()) + ")";
            }
        } else {
            // 旧路径兼容
            for(int i = 0; i < req->player_ids_size() && i < 2; ++i) {
                uint64_t aid = req->player_ids(i);
                bool isWin = false;
                for(int j = 0; j < req->winner_ids_size(); ++j) {
                    if(req->winner_ids(j) == aid) {
                        isWin = true;
                        break;
                    }
                }
                if(i > 0) sql += ",";
                sql += "(" + std::to_string(rid) + "," + std::to_string(aid)
                     + "," + std::to_string(i == 0 ? (int)TEAM_RED : (int)TEAM_BLUE)
                     + "," + std::to_string(isWin ? 1 : 0) + ",0)";
            }
        }
        if(trx.execute(sql) != 0) {
            trx.rollback();
            resp->set_result(FAIL);
            resp->set_msg("insert players fail");
            if(done) done->Run();
            return;
        }

        if(!trx.commit()) {
            resp->set_result(FAIL);
            resp->set_msg("commit fail");
            if(done) done->Run();
            return;
        }
        resp->set_result(SUCCESS);
        if(done) done->Run();
        return;
    }

    // 双通道: 事务在 dbTask 内部完成
    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                return;
            }

            bool useNew = reqCopy.players_size() > 0;
            if(!useNew && reqCopy.player_ids_size() == 0) {
                resp->set_result(BAD_PARAM);
                resp->set_msg("empty record");
                return;
            }

            sylar::Transaction trx(db.getConnection());
            if(!trx.begin()) {
                resp->set_result(FAIL);
                resp->set_msg("begin tx fail");
                return;
            }

            std::string mainSql = "INSERT INTO game_records(winning_team,duration) VALUES("
                + std::to_string((int)reqCopy.winning_team()) + "," + std::to_string(reqCopy.duration()) + ")";
            if(trx.execute(mainSql) != 0) {
                trx.rollback();
                resp->set_result(FAIL);
                resp->set_msg("insert main fail");
                return;
            }
            uint64_t rid = (uint64_t)trx.getLastInsertId();

            std::string sql = "INSERT INTO game_record_players(record_id,account_id,team,is_winner,damage_dealt) VALUES";
            if(useNew) {
                for(int i = 0; i < reqCopy.players_size(); ++i) {
                    const auto& p = reqCopy.players(i);
                    if(i > 0) sql += ",";
                    sql += "(" + std::to_string(rid)
                         + "," + std::to_string(p.account_id())
                         + "," + std::to_string((int)p.team())
                         + "," + std::to_string(p.is_winner() ? 1 : 0)
                         + "," + std::to_string(p.damage_dealt()) + ")";
                }
            } else {
                for(int i = 0; i < reqCopy.player_ids_size() && i < 2; ++i) {
                    uint64_t aid = reqCopy.player_ids(i);
                    bool isWin = false;
                    for(int j = 0; j < reqCopy.winner_ids_size(); ++j) {
                        if(reqCopy.winner_ids(j) == aid) {
                            isWin = true;
                            break;
                        }
                    }
                    if(i > 0) sql += ",";
                    sql += "(" + std::to_string(rid) + "," + std::to_string(aid)
                         + "," + std::to_string(i == 0 ? (int)TEAM_RED : (int)TEAM_BLUE)
                         + "," + std::to_string(isWin ? 1 : 0) + ",0)";
                }
            }
            if(trx.execute(sql) != 0) {
                trx.rollback();
                resp->set_result(FAIL);
                resp->set_msg("insert players fail");
                return;
            }

            if(!trx.commit()) {
                resp->set_result(FAIL);
                resp->set_msg("commit fail");
                return;
            }
            resp->set_result(SUCCESS);
        },
        [done]() {
            if(done) done->Run();
        }
    );
}

// ---- 好友 ----
void DataServiceImpl::AddFriend(::google::protobuf::RpcController*,
        const AddFriendReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            if(done) done->Run();
            return;
        }
        // 双向插入
        std::string sql = "INSERT IGNORE INTO friends(account_id,friend_id) VALUES("
            + std::to_string(req->account_id()) + "," + std::to_string(req->friend_id()) + "),("
            + std::to_string(req->friend_id()) + "," + std::to_string(req->account_id()) + ")";
        resp->set_result(db.execute(sql) ? SUCCESS : FAIL);
        if(done) done->Run();
        return;
    }

    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                return;
            }
            sylar::Connection* c = db.getConnection();
            // 双向插入(预处理)
            auto stmt = c->prepare("INSERT IGNORE INTO friends(account_id,friend_id) VALUES(?,?),(?,?)");
            if(!stmt) {
                resp->set_result(FAIL);
                return;
            }
            stmt->bindUint64(1, reqCopy.account_id());
            stmt->bindUint64(2, reqCopy.friend_id());
            stmt->bindUint64(3, reqCopy.friend_id());
            stmt->bindUint64(4, reqCopy.account_id());
            bool ok = (stmt->execute() == 0);
            resp->set_result(ok ? SUCCESS : FAIL);
        },
        [done]() {
            if(done) done->Run();
        }
    );
}

void DataServiceImpl::GetFriendList(::google::protobuf::RpcController*,
        const IdReq* req, FriendListRpcResp* resp, ::google::protobuf::Closure* done) {
    // 批量查在线状态: 必须在协程线程(Redis hook 在 DB 线程失效)
    auto fetchOnline = [this]() -> std::set<uint64_t> {
        std::set<uint64_t> onlineIds;
        if(!m_redisPool) return onlineIds;
        RedisGuard g(m_redisPool.get());
        if(!g) return onlineIds;
        redisReply* r = (redisReply*)redisCommand(g.get(), "SMEMBERS %s", "online:players");
        if(r && r->type == REDIS_REPLY_ARRAY) {
            for(size_t i = 0; i < r->elements; ++i) {
                if(r->element[i]->type == REDIS_REPLY_STRING) {
                    onlineIds.insert((uint64_t)strtoll(r->element[i]->str, nullptr, 10));
                }
            }
        }
        if(r) freeReplyObject(r);
        return onlineIds;
    };

    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            if(done) done->Run();
            return;
        }
        auto rows = db.query("SELECT a.id AS fid,a.name AS fname,p.level AS level FROM friends f "
            "JOIN accounts a ON f.friend_id=a.id "
            "LEFT JOIN player_profiles p ON p.account_id=a.id "
            "WHERE f.account_id=" + std::to_string(req->account_id()));
        resp->set_result(SUCCESS);

        auto onlineIds = fetchOnline();
        for(auto& r : rows) {
            auto* f = resp->add_friends();
            uint64_t fid = (uint64_t)(int64_t)r["fid"];
            f->set_account_id(fid);
            f->set_name((std::string)r["fname"]);
            f->set_level((!r["level"].isNull() ? (int32_t)(int64_t)r["level"] : 1));
            f->set_online(onlineIds.count(fid) > 0);
        }
        if(done) done->Run();
        return;
    }

    // 双通道: dbTask 只做 MySQL 查询(中间结果经 shared_ptr 传给 onComplete);
    //         onComplete 在协程线程查 Redis 在线状态后填 resp(hook 正常)。
    struct FriendRow { uint64_t fid; std::string fname; int32_t level; };
    auto rows = std::make_shared<std::vector<FriendRow>>();
    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, rows]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) return;
            sylar::Connection* c = db.getConnection();
            auto stmt = c->prepare("SELECT a.id AS fid,a.name AS fname,p.level AS level FROM friends f "
                "JOIN accounts a ON f.friend_id=a.id "
                "LEFT JOIN player_profiles p ON p.account_id=a.id "
                "WHERE f.account_id=?");
            if(!stmt) return;
            stmt->bindUint64(1, reqCopy.account_id());
            for(auto& r : stmt->queryRows()) {
                rows->push_back({
                    (uint64_t)(int64_t)r["fid"],
                    (std::string)r["fname"],
                    (!r["level"].isNull() ? (int32_t)(int64_t)r["level"] : 1)
                });
            }
        },
        [this, rows, resp, done, fetchOnline]() {
            resp->set_result(SUCCESS);
            auto onlineIds = fetchOnline();
            for(auto& fr : *rows) {
                auto* f = resp->add_friends();
                f->set_account_id(fr.fid);
                f->set_name(fr.fname);
                f->set_level(fr.level);
                f->set_online(onlineIds.count(fr.fid) > 0);
            }
            if(done) done->Run();
        }
    );
}

// ---- 聊天记录 ----
void DataServiceImpl::SaveChat(::google::protobuf::RpcController*,
        const SaveChatReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            if(done) done->Run();
            return;
        }
        sylar::Connection* c = db.getConnection();
        std::string sql = "INSERT INTO chat_history(channel,sender_id,sender_name,message,target_id) VALUES("
            + std::to_string((int)req->channel()) + "," + std::to_string(req->sender_id()) + ",'"
            + esc(c, req->sender_name()) + "','" + esc(c, req->message()) + "',"
            + std::to_string(req->target_id()) + ")";
        resp->set_result(db.execute(sql) ? SUCCESS : FAIL);
        if(done) done->Run();
        return;
    }

    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                return;
            }
            sylar::Connection* c = db.getConnection();
            auto stmt = c->prepare("INSERT INTO chat_history(channel,sender_id,sender_name,message,target_id) VALUES(?,?,?,?,?)");
            if(!stmt) {
                resp->set_result(FAIL);
                return;
            }
            stmt->bindInt32(1, (int32_t)reqCopy.channel());
            stmt->bindUint64(2, reqCopy.sender_id());
            stmt->bindString(3, reqCopy.sender_name());
            stmt->bindString(4, reqCopy.message());
            stmt->bindUint64(5, reqCopy.target_id());
            bool ok = (stmt->execute() == 0);
            resp->set_result(ok ? SUCCESS : FAIL);
        },
        [done]() {
            if(done) done->Run();
        }
    );
}

void DataServiceImpl::GetChatHistory(::google::protobuf::RpcController*,
        const GetChatHistoryReq* req, ChatHistoryRespRpc* resp, ::google::protobuf::Closure* done) {
    // 世界频道: 从 Redis List 读(不查 MySQL, 世界消息不落盘)
    if(req->channel() == CHANNEL_WORLD) {
        if(!m_redisPool) {
            resp->set_result(FAIL);
            if(done) {
                done->Run();
            }
            return;
        }
        int cnt = req->count() > 0 ? req->count() : 50;
        RedisGuard g(m_redisPool.get());
        if(!g) {
            resp->set_result(FAIL);
            if(done) {
                done->Run();
            }
            return;
        }
        // LRANGE chat:world 0 (cnt-1) → 最新在前(LPUSH), 反转成正序
        redisReply* r = (redisReply*)redisCommand(g.get(), "LRANGE %s 0 %d", "chat:world", cnt - 1);
        if(g.get()->err) {
            g.markUnhealthy();
        }
        resp->set_result(SUCCESS);
        if(r && r->type == REDIS_REPLY_ARRAY) {
            for(int i = (int)r->elements - 1; i >= 0; --i) {
                if(r->element[i]->type == REDIS_REPLY_STRING) {
                    ChatNotify n;
                    if(n.ParseFromArray(r->element[i]->str, r->element[i]->len)) {
                        auto* e = resp->add_entries();
                        e->set_sender_id(n.sender_id());
                        e->set_sender_name(n.sender_name());
                        e->set_message(n.message());
                        e->set_timestamp(n.timestamp());
                    }
                }
            }
        }
        if(r) {
            freeReplyObject(r);
        }
        if(done) {
            done->Run();
        }
        return;
    }

    // ROOM/TEAM 频道: 不落盘, 返回空
    if(req->channel() == CHANNEL_ROOM || req->channel() == CHANNEL_TEAM) {
        resp->set_result(SUCCESS);
        if(done) {
            done->Run();
        }
        return;
    }

    // 其他频道: 查 MySQL(回退路径, PRIVATE 有专用 GetPrivateHistory)
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            if(done) {
                done->Run();
            }
            return;
        }
        int cnt = req->count() > 0 ? req->count() : 50;
        auto rows = db.query("SELECT sender_id,sender_name,message,UNIX_TIMESTAMP(created_at) AS ts FROM chat_history "
            "WHERE channel=" + std::to_string((int)req->channel()) + " ORDER BY id DESC LIMIT " + std::to_string(cnt));
        resp->set_result(SUCCESS);
        for(auto it = rows.rbegin(); it != rows.rend(); ++it) {
            auto* e = resp->add_entries();
            e->set_sender_id((uint64_t)(int64_t)(*it)["sender_id"]);
            e->set_sender_name((std::string)(*it)["sender_name"]);
            e->set_message((std::string)(*it)["message"]);
            e->set_timestamp((uint64_t)(int64_t)(*it)["ts"]);
        }
        if(done) {
            done->Run();
        }
        return;
    }

    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                return;
            }
            sylar::Connection* c = db.getConnection();
            int cnt = reqCopy.count() > 0 ? reqCopy.count() : 50;
            auto stmt = c->prepare("SELECT sender_id,sender_name,message,UNIX_TIMESTAMP(created_at) AS ts FROM chat_history "
                "WHERE channel=? ORDER BY id DESC LIMIT " + std::to_string(cnt));
            if(!stmt) {
                resp->set_result(FAIL);
                return;
            }
            stmt->bindInt32(1, (int32_t)reqCopy.channel());
            auto rows = stmt->queryRows();
            resp->set_result(SUCCESS);
            for(auto it = rows.rbegin(); it != rows.rend(); ++it) {
                auto* e = resp->add_entries();
                e->set_sender_id((uint64_t)(int64_t)(*it)["sender_id"]);
                e->set_sender_name((std::string)(*it)["sender_name"]);
                e->set_message((std::string)(*it)["message"]);
                e->set_timestamp((uint64_t)(int64_t)(*it)["ts"]);
            }
        },
        [done]() {
            if(done) {
                done->Run();
            }
        }
    );
}

void DataServiceImpl::GetPrivateHistory(::google::protobuf::RpcController*,
        const GetPrivateHistoryReq* req, ChatHistoryRespRpc* resp, ::google::protobuf::Closure* done) {
    if(!m_dbPool) {
        sylar::Database db(m_pool.get());
        if(!db.valid()) {
            resp->set_result(FAIL);
            if(done) done->Run();
            return;
        }
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
        return;
    }

    auto reqCopy = *req;
    m_dbPool->query(
        [this, reqCopy, resp]() {
            sylar::Database db(m_pool.get());
            if(!db.valid()) {
                resp->set_result(FAIL);
                return;
            }
            sylar::Connection* c = db.getConnection();
            int cnt = reqCopy.count() > 0 ? reqCopy.count() : 50;
            auto stmt = c->prepare("SELECT sender_id,sender_name,message,UNIX_TIMESTAMP(created_at) AS ts FROM chat_history "
                "WHERE channel=? "
                "AND ((sender_id=? AND target_id=?) "
                "OR (sender_id=? AND target_id=?)) "
                "ORDER BY id DESC LIMIT " + std::to_string(cnt));
            if(!stmt) {
                resp->set_result(FAIL);
                return;
            }
            stmt->bindInt32(1, (int32_t)CHANNEL_PRIVATE);
            stmt->bindUint64(2, reqCopy.my_id());
            stmt->bindUint64(3, reqCopy.target_id());
            stmt->bindUint64(4, reqCopy.target_id());
            stmt->bindUint64(5, reqCopy.my_id());
            auto rows = stmt->queryRows();
            resp->set_result(SUCCESS);
            for(auto it = rows.rbegin(); it != rows.rend(); ++it) {
                auto* e = resp->add_entries();
                e->set_sender_id((uint64_t)(int64_t)(*it)["sender_id"]);
                e->set_sender_name((std::string)(*it)["sender_name"]);
                e->set_message((std::string)(*it)["message"]);
                e->set_timestamp((uint64_t)(int64_t)(*it)["ts"]);
            }
        },
        [done]() {
            if(done) done->Run();
        }
    );
}

// ---- 在线状态(Redis Set online:players) ----
// gate 在玩家登录/断线时调, lobby GetFriendList 时读。
void DataServiceImpl::SetOnline(::google::protobuf::RpcController*,
        const IdReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    // Redis 操作留协程同步(同 SaveToken: DB 线程下 hook 失效致 EAGAIN)
    if(!m_redisPool) {
        resp->set_result(FAIL);
        if(done) done->Run();
        return;
    }
    RedisGuard g(m_redisPool.get());
    if(!g) {
        resp->set_result(FAIL);
        if(done) done->Run();
        return;
    }
    redisReply* r = (redisReply*)redisCommand(g.get(), "SADD %s %llu",
        "online:players", (unsigned long long)req->account_id());
    bool ok = (r && r->type == REDIS_REPLY_INTEGER);
    if(g.get()->err) g.markUnhealthy();
    if(r) freeReplyObject(r);
    resp->set_result(ok ? SUCCESS : FAIL);
    if(done) done->Run();
}

void DataServiceImpl::SetOffline(::google::protobuf::RpcController*,
        const IdReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    // Redis 操作留协程同步(同 SaveToken: DB 线程下 hook 失效致 EAGAIN)
    if(!m_redisPool) {
        resp->set_result(FAIL);
        if(done) done->Run();
        return;
    }
    RedisGuard g(m_redisPool.get());
    if(!g) {
        resp->set_result(FAIL);
        if(done) done->Run();
        return;
    }
    redisReply* r = (redisReply*)redisCommand(g.get(), "SREM %s %llu",
        "online:players", (unsigned long long)req->account_id());
    bool ok = (r && r->type == REDIS_REPLY_INTEGER);
    if(g.get()->err) g.markUnhealthy();
    if(r) freeReplyObject(r);
    resp->set_result(ok ? SUCCESS : FAIL);
    if(done) done->Run();
}

// ---- 世界聊天广播(Redis PUBLISH chat:world) ----
// lobby 收到 CHAT(WORLD) 时调, data PUBLISH 后所有订阅的 gate 收到并 NotifyAllOnline。
void DataServiceImpl::PublishWorldChat(::google::protobuf::RpcController*,
        const SaveChatReq* req, ResultResp* resp, ::google::protobuf::Closure* done) {
    if(!m_redisPool) {
        resp->set_result(FAIL);
        if(done) {
            done->Run();
        }
        return;
    }

    // 世界频道不落 MySQL(高频无回看价值), 改为 Redis List 缓存最近 200 条 + PUBLISH。
    // 构造 ChatNotify payload
    ChatNotify notify;
    notify.set_channel(CHANNEL_WORLD);
    notify.set_sender_id(req->sender_id());
    notify.set_sender_name(req->sender_name());
    notify.set_message(req->message());
    notify.set_timestamp((uint64_t)time(nullptr));
    std::string payload;
    notify.SerializeToString(&payload);

    // Redis: LPUSH(写头部) + LTRIM(只留 200 条) + PUBLISH(推送 gate)
    RedisGuard g(m_redisPool.get());
    if(!g) {
        resp->set_result(FAIL);
        resp->set_msg("redis pool empty");
        if(done) {
            done->Run();
        }
        return;
    }
    redisReply* r1 = (redisReply*)redisCommand(g.get(), "LPUSH %s %b",
        "chat:world", payload.data(), payload.size());
    if(r1) {
        freeReplyObject(r1);
    }
    redisReply* r2 = (redisReply*)redisCommand(g.get(), "LTRIM %s 0 199", "chat:world");
    if(r2) {
        freeReplyObject(r2);
    }
    bool ok = redisPublish(*m_redisPool, "chat:world", payload);
    if(g.get()->err) {
        g.markUnhealthy();
    }
    resp->set_result(ok ? SUCCESS : FAIL);
    if(done) {
        done->Run();
    }
}

} // namespace ddt
