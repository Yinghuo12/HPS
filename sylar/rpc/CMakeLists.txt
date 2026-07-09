# RPC 层：rpc 源【不并入 libsylar】，仅由 rpc_test_callee/rpc_test_caller 直接编译，
# 避免与 rpc_test 重复链接造成符号冲突。
# 直接 set(... PARENT_SCOPE)（不经函数），子目录作用域的 PARENT 即根作用域，可正确上抛。
# 用绝对路径，与原 RPC_SRC 行为一致；force_redefine_file_macro_for_sources 据此算出相对 __FILE__。
set(RPC_SRC
    ${CMAKE_SOURCE_DIR}/sylar/rpc/rpc_controller.cc
    ${CMAKE_SOURCE_DIR}/sylar/rpc/rpc_provider.cc
    ${CMAKE_SOURCE_DIR}/sylar/rpc/rpc_channel.cc
    ${CMAKE_SOURCE_DIR}/sylar/rpc/zk_client.cc
    PARENT_SCOPE)
