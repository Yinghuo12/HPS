# MySQL ORM 层：自洽工具 / Value / Result / Stmt / Connection / Pool / Transaction / Query / Database
# model.h 为纯头文件模板，不参与编译
sylar_add_sources(
    util.cc
    value.cc
    result.cc
    stmt.cc
    connection.cc
    connection_pool.cc
    transaction.cc
    query.cc
    database.cc
)
