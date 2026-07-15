# RPC 层：编入 libsylar（含 rpc_controller/provider/channel/pool + etcd_client）。
# etcd_client.cc 依赖 etcd-cpp-apiv3（C++17），单独按 C++17 编译，其余源与 sylar 一起 C++11。
# 另外上抛 RPC_SRC 供 rpc_test_callee/rpc_test_caller 直接编译（历史用法，保留兼容）。
sylar_add_sources(
    rpc_controller.cc
    rpc_provider.cc
    rpc_channel.cc
    rpc_channel_pool.cc
    etcd_client.cc
)

set(RPC_SRC
    ${CMAKE_SOURCE_DIR}/sylar/rpc/rpc_controller.cc
    ${CMAKE_SOURCE_DIR}/sylar/rpc/rpc_provider.cc
    ${CMAKE_SOURCE_DIR}/sylar/rpc/rpc_channel.cc
    ${CMAKE_SOURCE_DIR}/sylar/rpc/etcd_client.cc
    PARENT_SCOPE)
