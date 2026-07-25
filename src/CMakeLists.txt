# ============================================================
# DDT 微服务: 5 个独立可执行目标 (gate/login/lobby/battle/data)
#
# 依赖: sylar(含 http/orm/jsoncpp) + etcd-cpp-api(RPC) + protobuf + mysqlclient + hiredis
# proto: common.proto + gate.proto + rpc.proto -> 生成 ${DDT_PB_DIR}/*.pb.cc
# ============================================================

# ---- protobuf 生成 ----
set(DDT_PROTO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/proto)
set(DDT_PB_DIR ${CMAKE_BINARY_DIR}/src_proto)
file(MAKE_DIRECTORY ${DDT_PB_DIR})

# 先生成 common.pb (gate/rpc 依赖它)
add_custom_command(
    OUTPUT ${DDT_PB_DIR}/common.pb.cc ${DDT_PB_DIR}/common.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --cpp_out=${DDT_PB_DIR} --proto_path=${DDT_PROTO_DIR}
        ${DDT_PROTO_DIR}/common.proto
    DEPENDS ${DDT_PROTO_DIR}/common.proto
    COMMENT "Generating common.pb"
)
add_custom_command(
    OUTPUT ${DDT_PB_DIR}/gate.pb.cc ${DDT_PB_DIR}/gate.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --cpp_out=${DDT_PB_DIR} --proto_path=${DDT_PROTO_DIR}
        ${DDT_PROTO_DIR}/gate.proto
    DEPENDS ${DDT_PROTO_DIR}/gate.proto ${DDT_PB_DIR}/common.pb.h
    COMMENT "Generating gate.pb"
)
add_custom_command(
    OUTPUT ${DDT_PB_DIR}/rpc.pb.cc ${DDT_PB_DIR}/rpc.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --cpp_out=${DDT_PB_DIR} --proto_path=${DDT_PROTO_DIR}
        ${DDT_PROTO_DIR}/rpc.proto
    DEPENDS ${DDT_PROTO_DIR}/rpc.proto ${DDT_PB_DIR}/common.pb.h
    COMMENT "Generating rpc.pb"
)
add_custom_target(ddt_srv_pb DEPENDS
    ${DDT_PB_DIR}/common.pb.cc ${DDT_PB_DIR}/common.pb.h
    ${DDT_PB_DIR}/gate.pb.cc ${DDT_PB_DIR}/gate.pb.h
    ${DDT_PB_DIR}/rpc.pb.cc ${DDT_PB_DIR}/rpc.pb.h)
include_directories(${DDT_PB_DIR})

# 各子目录的头文件可被任意服务源文件以 "xxx.h" 直接包含
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/common
    ${CMAKE_CURRENT_SOURCE_DIR}/server
    ${CMAKE_CURRENT_SOURCE_DIR}/server/gate
    ${CMAKE_CURRENT_SOURCE_DIR}/server/login
    ${CMAKE_CURRENT_SOURCE_DIR}/server/lobby
    ${CMAKE_CURRENT_SOURCE_DIR}/server/battle
    ${CMAKE_CURRENT_SOURCE_DIR}/server/data
)

# ---- 公共源(common + service_base) ----
# 注: redis_pool 仅 data 服务需要, 但当前 DDT_COMMON_SRC 编入全部服务。
#     redis_pool.cc 仅依赖 hiredis(已在 DDT_SRV_LIBS), 非空编译产物无副作用。
set(DDT_COMMON_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/common/frame.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/common/ddt_physics.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/common/terrain2d.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/server/service_base.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/server/data/redis_pool.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/server/data/db_worker.cc
)
set(DDT_PB_SRC
    ${DDT_PB_DIR}/common.pb.cc
    ${DDT_PB_DIR}/gate.pb.cc
    ${DDT_PB_DIR}/rpc.pb.cc
)

# 公共链接库: sylar + etcd-cpp-api + protobuf + mysqlclient + hiredis
set(DDT_SRV_LIBS
    sylar
    ${YAML_CPP_LINK}
    -lpthread -ldl -lssl -lcrypto -ljsoncpp
    -lmysqlclient -lz -lzstd -lresolv -lhiredis
    ${Protobuf_LIBRARIES}
    etcd-cpp-api
)

# 工具宏: 定义一个服务可执行目标
#   srv_name / 源文件(除 DDT_COMMON_SRC + DDT_PB_SRC)
function(ddt_add_service target_name)
    add_executable(${target_name}
        ${ARGN}
        ${DDT_COMMON_SRC}
        ${DDT_PB_SRC}
    )
    add_dependencies(${target_name} ddt_srv_pb sylar)
    target_link_libraries(${target_name} ${DDT_SRV_LIBS})
    force_redefine_file_macro_for_sources(${target_name})
    set_target_properties(${target_name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
endfunction()

# ---- gate ----
ddt_add_service(ddt_gate
    ${CMAKE_CURRENT_SOURCE_DIR}/server/gate/gate_server.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/server/gate/gate_main.cc
)

# ---- login ----
ddt_add_service(ddt_login
    ${CMAKE_CURRENT_SOURCE_DIR}/server/login/login_service.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/server/login/login_main.cc
)

# ---- lobby ----
ddt_add_service(ddt_lobby
    ${CMAKE_CURRENT_SOURCE_DIR}/server/lobby/lobby_service.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/server/lobby/lobby_main.cc
)

# ---- battle ----
ddt_add_service(ddt_battle
    ${CMAKE_CURRENT_SOURCE_DIR}/server/battle/battle_room.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/server/battle/battle_service.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/server/battle/battle_main.cc
)

# ---- data ----
ddt_add_service(ddt_data
    ${CMAKE_CURRENT_SOURCE_DIR}/server/data/data_service.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/server/data/data_main.cc
)

# ---- test_rpc_smoke: 跨服务 RPC 冒烟（经 etcd 发现调 login/lobby/data）----
# 验证 5 服务骨架的 RPC 调用链端到端通畅。需带 LD_PRELOAD=lib/libetcd_close_fix.so 运行。
add_executable(test_rpc_smoke
    ${CMAKE_SOURCE_DIR}/tests/test_rpc_smoke.cc
    ${DDT_PB_DIR}/common.pb.cc
    ${DDT_PB_DIR}/rpc.pb.cc
)
add_dependencies(test_rpc_smoke ddt_srv_pb sylar)
target_link_libraries(test_rpc_smoke ${DDT_SRV_LIBS})
force_redefine_file_macro_for_sources(test_rpc_smoke)
set_target_properties(test_rpc_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# ---- test_battle_smoke: lobby→battle 战斗闭环冒烟 ----
# 直接调 BattleService.EnterBattle + 经 lobby 建房/加入/双ready 触发交接。需带 LD_PRELOAD 运行。
add_executable(test_battle_smoke
    ${CMAKE_SOURCE_DIR}/tests/test_battle_smoke.cc
    ${DDT_PB_DIR}/common.pb.cc
    ${DDT_PB_DIR}/rpc.pb.cc
)
add_dependencies(test_battle_smoke ddt_srv_pb sylar)
target_link_libraries(test_battle_smoke ${DDT_SRV_LIBS})
force_redefine_file_macro_for_sources(test_battle_smoke)
set_target_properties(test_battle_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")

# ---- test_save_record_smoke: §13 多人战绩落库(拆主子表)验证 ----
add_executable(test_save_record_smoke
    ${CMAKE_SOURCE_DIR}/tests/test_save_record_smoke.cc
    ${DDT_PB_DIR}/common.pb.cc
    ${DDT_PB_DIR}/rpc.pb.cc
)
add_dependencies(test_save_record_smoke ddt_srv_pb sylar)
target_link_libraries(test_save_record_smoke ${DDT_SRV_LIBS})
force_redefine_file_macro_for_sources(test_save_record_smoke)
set_target_properties(test_save_record_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
set_target_properties(test_battle_smoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")
