cmake_minimum_required(VERSION 3.5)

# Proto 生成目录
set(DDT_PB_DIR ${CMAKE_BINARY_DIR}/proto)

set(DDT_SERVER_SRC
    gate/ddt_main.cc
    gate/ddt_servlet.cc
    gate/ddt_session_mgr.cc
    lobby/ddt_auth.cc
    lobby/ddt_chat_manager.cc
    match/ddt_player.cc
    match/ddt_game_room.cc
    match/ddt_room_manager.cc
    config/ddt_config.cc
    db/ddt_database.cc
    ${DDT_PB_DIR}/ddt.pb.cc
)

add_executable(ddt_server ${DDT_SERVER_SRC})
add_dependencies(ddt_server ddt_pb)

target_include_directories(ddt_server PRIVATE
    ${DDT_PB_DIR}
    ${CMAKE_SOURCE_DIR}/src/server/gate
    ${CMAKE_SOURCE_DIR}/src/server/lobby
    ${CMAKE_SOURCE_DIR}/src/server/match
    ${CMAKE_SOURCE_DIR}/src/server/db
    ${CMAKE_SOURCE_DIR}/src/server/config
)

target_link_libraries(ddt_server sylar ddt_physics_common ${DEPS_LIBS} protobuf mysqlclient hiredis)

set_target_properties(ddt_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)
