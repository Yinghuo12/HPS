# sylar/orm —— MySQL ORM 模块

一个自洽的、零侵入的 MySQL ORM 模块,实现于 `sylar/orm/` 目录下,**不依赖也不修改 sylar 之外的任何源码**。仅复用 sylar 现有可编译的基础设施(`sylar::Logger`、`sylar::Mutex`、`sylar::Time2Str` 等),其余需要的工具(字符串格式化、类型转换、SQL 转义等)在本目录内自行实现。

- 命名风格贴近常见 ORM 文档:`Model` / `Database` / `Connection` / `ConnectionPool` / `Value` / `Transaction` / `Query`。
- 命名空间统一为 `sylar`,与框架无缝衔接。
- 编译标准 C++11,在 `-Werror -Wall` 下零警告。

---

## 一、功能总览

| 类别 | 能力 |
|---|---|
| **实体建模** | `Model<T>` CRTP 基类,派生类只需声明 `table()` 与 `primary_key()` |
| **字段访问** | `obj["name"] = "jack"` 赋值;`obj("id")` 取值(自动类型转换) |
| **CRUD** | `save`(自动判别 INSERT/UPDATE + 回填自增主键)/ `insert(批量)` / `update` / `remove` / `truncate` |
| **查询** | `one()` / `all()` / `batch(size)` 分页迭代器(支持范围 for 与老式迭代器两套写法) |
| **聚合** | `count` / `sum` / `min` / `max` / `average` |
| **单值/列** | `scalar(field)` / `column(field)` / `exists()` |
| **链式子句** | `where`(5 种重载)/ `select` / `alias` / `join` / `group` / `having` / `order` / `limit` / `offset` |
| **where 运算符** | `=`、`!=`/`<>`、`<`、`<=`、`>`、`>=`、`in`、`not in`、`like`、`not like`、`is`、`is not`、`between`、`not between` |
| **事务** | `Transaction`,支持嵌套(内层用 SAVEPOINT,语义正确) |
| **连接池** | `ConnectionPool`:`size` / `create` / `get` / `put` / `ping` 心跳 / `checkConnections` / 自动重连修复失效连接 |
| **预处理** | `PreparedStmt`:输入绑定 + 完整输出绑定查询,防 SQL 注入 |
| **原生 SQL** | `Database::query(sql)` / `Database::execute(sql)` / `Connection::query` / `Connection::execute` |
| **SQL 调试** | `obj.sql()` 取最近一次生成/执行的 SQL |

---

## 二、架构与文件结构

```
sylar/orm/
├── util.h / util.cc              # 自洽工具:Formatv/TypeUtil/Str2Time/MysqlEscape/GetParamValue/nop
├── value.h / value.cc            # 多类型 Value + 隐式转换 + toSql()
├── result.h / result.cc          # 结果集游标(列名↔下标,按名/按位取值,fetchAll)
├── stmt.h / stmt.cc              # 预处理 PreparedStmt
├── connection.h / connection.cc  # 单连接 connect/query/execute/prepare/escape/ping/reconnect
├── connection_pool.h / .cc       # 连接池 + 心跳 + 重连
├── transaction.h / .cc           # 事务(嵌套用 SAVEPOINT)
├── query.h / query.cc            # 链式 SQL 构建器 + 聚合 + 执行
├── database.h / database.cc      # Model 入口(单连接/连接池)
├── model.h                       # Model<T> CRTP + Batch<T> 分页迭代器(纯头文件模板)
└── README.md                     # 本文件
```

### 层次与数据流

```
   ┌──────────── ORM 层 ────────────┐
 Model<T>  →  Query(链式 where/join/…)  →  生成 SQL
   │              │                          │
   └─ Value(取值/赋值)                       ▼
 Database ──► Connection ──► MySQL C API (mysql_real_query / store_result / stmt)
                ▲
        ConnectionPool(size / get / put / ping 心跳 / auto_reconnect)
```

- 上层 `Model` / `Query` / `Value` 面向业务,提供链式 DSL 与对象映射。
- 中层 `Database` 是适配入口,把连接(单条或池)交给 `Model`。
- 底层 `Connection` / `Result` / `PreparedStmt` / `Transaction` 直接封装 MySQL C API。

---

## 三、快速上手

### 1. 定义实体类

```cpp
#include "sylar/orm/model.h"

class User : public sylar::Model<User> {
public:
    User() : Model() {}
    User(sylar::Database& db) : Model(db) {}
    User(sylar::Connection* conn) : Model(conn) {}

    std::string table()       const { return "t_user"; }
    std::string primary_key() const { return "id"; }
};
```

> 派生类**必须**实现 `table()` 与 `primary_key()` 两个方法(CRTP 要求)。

### 2. 建连接池 + Database

```cpp
sylar::ConnectionPool pool;
pool.size(10);
// 参数: host, port, user, password, dbname, charset, auto_reconnect
pool.create("127.0.0.1", 3306, "root", "pwd", "test", "utf8", true);
pool.ping(3600);   // 后台心跳间隔(秒),默认 3600

sylar::Database db(pool);   // 从池借出一条连接,db 析构时自动归还
```

也可以用单条连接(不经过池):

```cpp
std::map<std::string, std::string> params;
params["host"] = "127.0.0.1";
params["port"] = "3306";
params["user"] = "root";
params["passwd"] = "pwd";
params["dbname"] = "test";
sylar::Connection conn(params);
conn.connect();
sylar::Database db(&conn);   // 借用,不持有
```

### 3. 新增

```cpp
// 单条
User u(db);
u["name"]  = "jack";
u["age"]   = 18;
u["money"] = 100.0;
u.save();                 // 无主键 → INSERT,成功后回填自增 id

// 批量 10w
std::vector<User> rows;
for (int i = 0; i < 100000; ++i) {
    User r;
    r["name"]  = "name_" + std::to_string(i);
    r["age"]   = 20;
    r["money"] = 100;
    rows.push_back(r);
}
User(db).insert(rows);
```

### 4. 查询

```cpp
// 单条
auto user = User(db).where("name", "jack").one();
int id          = user("id");
std::string nm  = user("name");
int age         = user("age");
double money    = user("money");

// 多条
auto all = User(db).where("age", ">=", 18).order("age asc").all();
for (const auto& one : all) {
    std::cout << one.str() << std::endl;
}

// 分页批量(范围 for)
auto batch = User(db).order("id asc").batch(1000);
for (const auto& page : batch) {          // 每页 1000 条
    for (const auto& one : page) {
        std::cout << one.str() << std::endl;
    }
}
// 老式迭代器写法也支持:
// for (auto it = batch.begin(); it != batch.end(); ++it)
//     for (auto row = it.begin(); row != it.end(); ++row) ...
```

### 5. 修改

```cpp
// 方式一:查询后修改
auto user = User(db).where("name", "lucy").one();
user["age"]   = 30;
user["money"] = 300;
user.save();                  // 有主键 → UPDATE WHERE id=...

// 方式二:原地条件更新
User(db).where("id", 1).update({
    {"name", "sean"},
    {"age",  28}
});
```

### 6. 删除

```cpp
// 按主键(查到后删)
auto one = User(db).where("id", 3).one();
one.remove();

// 按条件批量删
User(db).where("name", "in", {sylar::Value("duke"), sylar::Value("sean")}).remove();
```

### 7. 聚合 / 单值

```cpp
int64_t total     = User(db).count();
double  sumMoney  = User(db).sum("money");
double  maxMoney  = User(db).max("money");
double  avgAge    = User(db).where("id", "in", {sylar::Value(1), sylar::Value(2)}).average("age");

int age = User(db).where("id", 3).scalar("age");
std::vector<sylar::Value> names =
    User(db).where("id", "in", {sylar::Value(1), sylar::Value(2), sylar::Value(3)}).column("name");
bool exists = User(db).where("name", "jack").exists();
```

### 8. 联表 / 分组

```cpp
auto model = User(db);
model.select("u.*").alias("u").join("t_fans", "f", "u.id=f.uid");
auto all = model.where("f.fid", "=", 4).group("u.age").having("money > 100")
                .order("age asc").limit(10).offset(0).all();

// 生成的 SQL 可通过 model.sql() 查看
```

### 9. 事务(支持嵌套)

```cpp
sylar::Connection* conn = pool.get();
sylar::Transaction trx(conn);
trx.begin();                 // 外层 → BEGIN

User u1(conn); u1["name"]="jack"; u1["age"]=18; u1["money"]=100;
if (!u1.save()) trx.rollback();

{
    trx.begin();             // 内层 → SAVEPOINT sp_1
    User u2(conn); u2["name"]="lucy"; u2["age"]=20; u2["money"]=200;
    if (!u2.save()) trx.rollback();
    trx.commit();            // 内层 → RELEASE SAVEPOINT sp_1
}
trx.commit();                // 外层 → COMMIT
pool.put(conn);
```

> MySQL 同一会话不支持真正的嵌套 `BEGIN`,本模块用 **SAVEPOINT** 实现嵌套语义:`begin()` 计数自增,外层走 `BEGIN/COMMIT/ROLLBACK`,内层走 `SAVEPOINT/RELEASE/ROLLBACK TO`。事务对象析构时若未显式结束,会整体回滚。**事务不能跨连接(会话)。**

### 10. 预处理(防注入)

```cpp
sylar::PreparedStmt::ptr st = conn->prepare("INSERT INTO t_user(name,age) VALUES(?,?)");
st->bindString(1, "o'brien");   // 自动转义,防注入
st->bindInt32(2, 25);
st->execute();
int64_t id = st->getLastInsertId();

// 预处理查询(直接物化为字段名->值 的行集合)
auto st2 = conn->prepare("SELECT id,name FROM t_user WHERE age >= ?");
st2->bindInt32(1, 18);
auto rows = st2->queryRows();
```

### 11. 原生 SQL

```cpp
sylar::Database db(pool);
auto rows = db.query("SELECT * FROM t_user WHERE age > 18");
for (const auto& row : rows) {
    std::cout << row.at("name").str() << std::endl;
}
db.execute("UPDATE t_user SET money=500 WHERE id=4");
```

---

## 四、测试

测试程序:[`framework/tests/test_orm.cc`](../../framework/tests/test_orm.cc)

### 覆盖内容

- **Value**:整型/浮点/字符串/NULL 转换、`toSql()` 字面量生成
- **Query SQL 生成**:`buildSelect` / `buildDelete`(in)/ `between` / `buildInsert` / `buildUpdate` / `buildCount` / `buildAggregate`
- **Model 模板**:`operator()` 取值、`operator[]` 赋值、`str()` 调试输出
- **ConnectionPool**:`size()` / `ping()` 配置
- **Batch**:批量插入 SQL、分页迭代器(无连接时安全立即结束)
- **Transaction**:无连接时安全早退
- **链式 join**:多表 JOIN + where + order 的 SQL 生成

> 该测试**不依赖真实数据库**:需要连接的方法在无连接时安全早退,主要验证模板实例化、SQL 生成正确性与类型转换。

### 构建与运行

```bash
cd /home/yinghuo/code/proj/sylar/build
cmake .. && make test_orm -j

# 运行(需能找到 libsylar.so)
cd /home/yinghuo/code/proj/sylar
LD_LIBRARY_PATH=./lib ./bin/test_orm
```

预期输出结尾:`==== ALL PASS ====`。

### 真实数据库端到端测试(可选)

如需对真实 MySQL 做端到端验证,可在测试中创建表后跑全流程:

```sql
CREATE TABLE t_user (
  id INT AUTO_INCREMENT PRIMARY KEY,
  name VARCHAR(64) NOT NULL,
  age INT,
  money DOUBLE
);
```

然后 `pool.create(...)` 连上,依次调用 `save / one / all / count / sum / update / remove / truncate` 校验数据一致。

---

## 五、构建说明(CMake)

本模块已挂入根 [CMakeLists.txt](../../CMakeLists.txt):

1. ORM 源文件加入 `sylar` 共享库的 `LIB_SRC`(`util/value/result/stmt/connection/connection_pool/transaction/query/database` 共 9 个 `.cc`;`model.h` 为纯头文件模板,无需编译)。
2. 链接库新增 `-lmysqlclient -lz -lzstd -lresolv`(其余 `-lssl -lcrypto -lpthread -ldl` 框架已有)。
3. 新增 `test_orm` 可执行目标。

依赖系统包:`libmysqlclient-dev`(MySQL 8.0 C API)。

---

## 六、设计要点

- **自洽工具层**(`util.*`):上游 sylar 的 `StringUtil/TypeUtil/mutex.h/nop/Str2Time` 在本工程中不存在,本模块在目录内自行实现,因此**无需改动任何 sylar 之外的文件**。
- **连接池借还协调**:用 `std::mutex + std::condition_variable`(谓词 `!m_conns.empty() || m_created < m_maxSize`),容量上限由 `m_created` 计数保证,池空且达上限时阻塞等待归还。
- **健康检查与重连**:借出连接时,若空闲较久或曾报错则 `ping()`,失败则 `connect()` 重连,仍失败则丢弃重建——实现"自动修复失效连接"。
- **后台心跳**:`ping(sec)` 设置间隔,池启动一个独立线程定时 `ping` 所有空闲连接,防服务端超时断开;池析构时安全停止。
- **事务嵌套**:用 SAVEPOINT,而非文档里可能误导的"嵌套 BEGIN"。
- **SQL 转义**:`Value::toSql()` 对字符串做 MySQL 标准转义(`' \ " \0 \n \r \x1a`),拼接 SQL 时调用;预处理路径则用 `mysql_stmt_bind_*` 参数化,彻底防注入。
- **CRTP 链式**:`where/select/...` 返回派生类引用(`static_cast<T&>(*this)`),`User(db).where(...).one()` 连写自然;每个终结操作(one/all/save/count/…)执行后会清空查询子句,避免跨操作污染。
