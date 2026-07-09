// ORM 模块自测：验证 Model 模板能正确实例化、SQL 生成正确、Value 转换正确。
// 不依赖真实数据库（需要 DB 的方法会在无连接时安全早退）。
#include "sylar/orm/model.h"
#include "sylar/orm/connection.h"
#include "sylar/orm/connection_pool.h"
#include "sylar/orm/transaction.h"
#include "sylar/orm/database.h"
#include <iostream>
#include <vector>
#include <string>

using namespace sylar;

// 示例实体：t_user(id, name, age, money)
class User : public Model<User> {
public:
    User() : Model() {}
    User(Database& db) : Model(db) {}
    User(Connection* conn) : Model(conn) {}

    std::string table() const { return "t_user"; }
    std::string primary_key() const { return "id"; }
};

class Fans : public Model<Fans> {
public:
    Fans() : Model() {}
    std::string table() const { return "t_fans"; }
    std::string primary_key() const { return "id"; }
};

static int g_fail = 0;
static void check(bool cond, const std::string& name, const std::string& got, const std::string& want) {
    if(cond) {
        std::cout << "[ OK ] " << name << std::endl;
    } else {
        std::cout << "[FAIL] " << name << "\n   got : " << got << "\n   want: " << want << std::endl;
        ++g_fail;
    }
}

int main() {
    // ---- Value 转换 ----
    Value vi(18), vd(100.5), vs("jack"), vn;
    check((int)vi == 18, "Value int", std::to_string((int)vi), "18");
    check((double)vd == 100.5, "Value double", std::to_string((double)vd), "100.5");
    check((std::string)vs == "jack", "Value string", (std::string)vs, "jack");
    check(vn.isNull(), "Value null", vn.isNull()?"null":"notnull", "null");
    check(vs.toSql() == "'jack'", "Value toSql string", vs.toSql(), "'jack'");
    check(vi.toSql() == "18", "Value toSql int", vi.toSql(), "18");

    // ---- Query SQL 生成（不依赖连接） ----
    Query q((Connection*)nullptr, "t_user");
    std::string sql = q.where("age", ">=", 18).where("name", "like", "ja%")
        .order("age asc").limit(10).offset(5).buildSelect();
    check(sql == "SELECT * FROM t_user WHERE (age >= 18) AND (name like 'ja%') "
                 "ORDER BY age asc OFFSET 5 LIMIT 10",
          "Query buildSelect", sql, "SELECT * ...");

    q.clear();
    sql = q.where("id", "in", {Value(1), Value(2)}).buildDelete();
    check(sql == "DELETE FROM t_user WHERE (id IN (1,2))",
          "Query buildDelete in", sql, "DELETE ... IN");

    q.clear();
    sql = q.where("money", "between", 200, 300).buildSelect();
    check(sql == "SELECT * FROM t_user WHERE (money BETWEEN 200 AND 300)",
          "Query between", sql, "SELECT ... BETWEEN");

    q.clear();
    std::map<std::string, Value> row;
    row["name"] = "jack";
    row["age"] = 18;
    sql = q.buildInsert(row);
    // std::map 按键名字母序：age, name
    check(sql == "INSERT INTO t_user (age,name) VALUES (18,'jack')",
          "Query buildInsert", sql, "INSERT ...");

    q.clear();
    sql = q.buildUpdate(row);
    check(sql == "UPDATE t_user SET age=18,name='jack'", "Query buildUpdate", sql, "UPDATE ...");

    q.clear();
    sql = q.buildCount();
    check(sql == "SELECT COUNT(*) AS cnt FROM t_user", "Query buildCount", sql, "COUNT");

    q.clear();
    sql = q.buildAggregate("SUM", "money");
    check(sql == "SELECT SUM(money) AS agg FROM t_user", "Query buildAggregate", sql, "SUM");

    // ---- Model 模板实例化与字段访问 ----
    User u;
    u["name"] = "jack";
    u["age"] = 18;
    u["money"] = 100.0;
    check((int)u("age") == 18, "Model operator()", std::to_string((int)u("age")), "18");
    check((std::string)u("name") == "jack", "Model operator() str", (std::string)u("name"), "jack");
    std::cout << "Model.str() => " << u.str() << std::endl;

    // ---- 连接池构造（不连接，仅验证 API/对象构造无误） ----
    ConnectionPool pool;
    pool.size(3);
    check(pool.size() == 3, "ConnectionPool size", std::to_string(pool.size()), "3");
    pool.ping(60);
    check(pool.ping() == 60, "ConnectionPool ping", std::to_string(pool.ping()), "60");

    // ---- 批量插入 SQL（不执行） ----
    Query qi2((Connection*)nullptr, "t_user");
    std::vector<std::map<std::string, Value> > rows;
    for(int i = 0; i < 3; ++i) {
        std::map<std::string, Value> r;
        r["name"] = std::string("n") + std::to_string(i);
        r["age"] = 20;
        rows.push_back(r);
    }
    sql = qi2.buildInsertBatch(rows);
    std::cout << "Batch insert SQL => " << sql << std::endl;

    // ---- 链式 join 生成 ----
    Query qj((Connection*)nullptr, "t_user");
    qj.select("u.*").alias("u").join("t_fans", "f", "u.id=f.uid");
    sql = qj.where("f.fid", "=", 4).order("age asc").buildSelect();
    std::cout << "Join SQL => " << sql << std::endl;

    // ---- Batch 分页迭代器类型构造（不连库，迭代立即结束） ----
    User ub;
    Batch<User> bch = ub.batch(10);
    int pages = 0;
    for(typename std::vector<User>::size_type /*unused*/ x = 0; x < 0; ++x) {}
    // 无连接时迭代器取不到数据，应立即结束
    for(const std::vector<User>& page : bch) {
        (void)page;
        ++pages;
    }
    check(pages == 0, "Batch no-conn ends immediately", std::to_string(pages), "0");

    // ---- Transaction 嵌套 SQL（不连库，仅构造） ----
    {
        Transaction trx(nullptr); // 无连接，方法会安全早退
        check(trx.begin() == false, "Transaction no-conn begin safe", "false", "false");
    }

    std::cout << "\n==== " << (g_fail == 0 ? "ALL PASS" : "HAS FAILURES") << " ====" << std::endl;
    return g_fail == 0 ? 0 : 1;
}
