cmake_minimum_required(VERSION 3.14)
project(sylar)

include(FetchContent)
include(cmake/utils.cmake)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -ggdb -std=c++17 -Wall -Wno-deprecated -Wno-unused-function -Wno-builtin-macro-redefined -Wno-deprecated-declarations -Wno-pessimizing-move")

# === OpenSSL ===
find_package(OpenSSL REQUIRED)
include_directories(${OPENGL_INCLUDE_DIR})

# === 头文件 ===
include_directories(.)
include_directories(./thirdparty/include)
include_directories(./thirdparty/include/jsoncpp)

# === Boost (headers) ===
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/include/boost/boost")
    include_directories(./thirdparty/include/boost)
    message(STATUS "Boost: using local headers")
else()
    find_package(Boost QUIET)
    if(Boost_FOUND)
        include_directories(${Boost_INCLUDE_DIRS})
        message(STATUS "Boost: using system package")
    else()
        message(STATUS "Boost: downloading headers (~120MB, first build only)...")
        FetchContent_Declare(boost_src
            URL https://github.com/boostorg/boost/releases/download/boost-1.84.0/boost-1.84.0.tar.gz
        )
        FetchContent_Populate(boost_src)
        include_directories(${boost_src_SOURCE_DIR})
        message(STATUS "Boost: downloaded to ${boost_src_SOURCE_DIR}")
    endif()
endif()

# === yaml-cpp ===
if(EXISTS "${CMAKE_SOURCE_DIR}/thirdparty/include/yaml-cpp/yaml-cpp")
    include_directories(./thirdparty/include/yaml-cpp)
    set(YAML_CPP_LINK "-lyaml-cpp")
    message(STATUS "yaml-cpp: using local copy")
else()
    find_package(yaml-cpp QUIET)
    if(yaml-cpp_FOUND)
        set(YAML_CPP_LINK "yaml-cpp")
        message(STATUS "yaml-cpp: using system package")
    else()
        message(STATUS "yaml-cpp: building from source...")
        FetchContent_Declare(yaml-cpp
            URL https://github.com/jbeder/yaml-cpp/archive/refs/tags/0.8.0.tar.gz
        )
        set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(yaml-cpp)
        set(YAML_CPP_LINK "yaml-cpp")
        message(STATUS "yaml-cpp: built from source")
    endif()
endif()

# 库目录
link_directories(./lib)


# 源文件：各子目录通过 sylar_add_sources() 把 .cc 累积到全局属性 sylar_sources，
# 此处统一取回编入单 SHARED 库。路径为仓库相对路径（如 sylar/core/log.cc），
# 与原扁平 LIB_SRC 形态一致，__FILE__ 与 .o 命名行为不变。
# 层级依赖：util/(叶) ← core/ ← scheduler/ ← net/ ← http/orm/rpc/
# === etcd-cpp-apiv3（C++17 + gRPC）：编入 libsylar，供 rpc 模块使用 ===
find_package(etcd-cpp-api CONFIG QUIET)
if(etcd-cpp-api_FOUND)
    message(STATUS "etcd-cpp-apiv3: found (target etcd-cpp-api)")
else()
    message(WARNING
        "etcd-cpp-apiv3 NOT found. RPC/etcd 编码将无法链接。\n"
        "  vcpkg: vcpkg install etcd-cpp-apiv3\n"
        "  源码: https://github.com/etcd-cpp-apiv3/etcd-cpp-apiv3")
endif()

set(SYLAR_SOURCES "")
add_subdirectory(sylar/core)
add_subdirectory(sylar/util)
add_subdirectory(sylar/scheduler)
add_subdirectory(sylar/net)
add_subdirectory(sylar/http)
add_subdirectory(sylar/orm)
add_subdirectory(sylar/rpc)

# rpcheader.proto 生成并编入 libsylar（rpc_provider/channel 依赖 RpcHeader）
find_package(Protobuf REQUIRED)
set(RPC_PB_DIR ${CMAKE_BINARY_DIR}/rpc)
file(MAKE_DIRECTORY ${RPC_PB_DIR})
add_custom_command(
    OUTPUT ${RPC_PB_DIR}/rpcheader.pb.cc ${RPC_PB_DIR}/rpcheader.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --cpp_out=${RPC_PB_DIR} --proto_path=${CMAKE_SOURCE_DIR}/sylar/rpc
        ${CMAKE_SOURCE_DIR}/sylar/rpc/rpcheader.proto
    DEPENDS ${CMAKE_SOURCE_DIR}/sylar/rpc/rpcheader.proto
    COMMENT "Generating rpcheader.pb")
set_property(GLOBAL APPEND PROPERTY sylar_sources "${RPC_PB_DIR}/rpcheader.pb.cc")
include_directories(${RPC_PB_DIR})

get_property(SYLAR_SOURCES GLOBAL PROPERTY sylar_sources)


# 编译库
add_library(sylar SHARED ${SYLAR_SOURCES})
force_redefine_file_macro_for_sources(sylar) #__FILE__

# libsylar.so 自带其依赖的 DT_NEEDED：链接 sylar 的目标无需再显式罗列 mysqlclient/z/zstd/resolv 等，
# 修正 ddt_client 等仅链接 sylar 的消费者链接失败（PUBLIC：同时上抛给消费者，最稳）。
target_link_libraries(sylar PUBLIC
    ${YAML_CPP_LINK}
    -lpthread
    -ldl
    -lssl
    -lcrypto
    -ljsoncpp
    -lmysqlclient
    -lz
    -lzstd
    -lresolv
    ${Protobuf_LIBRARIES})

# etcd-cpp-apiv3 编入 libsylar（rpc/etcd_client.cc 用）；PUBLIC 上抛给消费者(ddt_* 服务)。
if(etcd-cpp-api_FOUND)
    target_link_libraries(sylar PUBLIC etcd-cpp-api)
endif()

# RPC_SRC 已由上方的 add_subdirectory(sylar/rpc) 提供并编入 libsylar
# （rpc_test_* 仍可直接编译 RPC_SRC，但 libsylar 自带 RPC 符号，消费者无需重复）。
# 注: 原 ddt_physics_common 静态库 target 已移除——五个 ddt_* 服务实际通过
# src/CMakeLists.txt 的 DDT_COMMON_SRC 直接把 ddt_physics.cc 编进各自可执行,
# 该 .a 从未被任何 target 链接, 属 dead target。

# 统一链接所有以来库
set(DEPS_LIBS
    sylar
    ${YAML_CPP_LINK}
    -lpthread
    -ldl
    -lssl
    -lcrypto
    -ljsoncpp
    -lmysqlclient
    -lz
    -lzstd
    -lresolv
)



# 测试程序
add_executable(test tests/test.cc)
add_executable(test_config tests/test_config.cc)
add_executable(test_thread tests/test_thread.cc)
add_executable(test_util tests/test_util.cc)
add_executable(test_fiber tests/test_fiber.cc)
add_executable(test_scheduler tests/test_scheduler.cc)
add_executable(test_iomanager tests/test_iomanager.cc)
add_executable(test_timer tests/test_timer.cc)
add_executable(test_timewheel tests/test_timewheel.cc)  # 三级时间轮(与 TimerManager 互补)
add_executable(test_hook tests/test_hook.cc)
add_executable(test_address tests/test_address.cc)
add_executable(test_socket tests/test_socket.cc)
add_executable(test_bytearray tests/test_bytearray.cc)
add_executable(test_http tests/test_http.cc)
add_executable(test_http_parser tests/test_http_parser.cc)
add_executable(test_tcp_server tests/test_tcp_server.cc)
add_executable(echo_server sylar/examples/echo_server.cc)
add_executable(test_http_server tests/test_http_server.cc)
add_executable(test_http_connection tests/test_http_connection.cc)
add_executable(test_uri tests/test_uri.cc)
add_executable(my_http_server sylar/examples/my_http_server.cc)
add_executable(test_daemon tests/test_daemon.cc)
add_executable(test_env tests/test_env.cc)
add_executable(test_asynclog tests/test_asynclog.cc)  # 二次开发异步日志
add_executable(test_orm tests/test_orm.cc)  # ORM 模块自测
add_executable(test_etcd tests/test_etcd.cc)  # etcd 封装端到端自测（需本机 2379 起好 etcd）
add_executable(echo_server_udp sylar/examples/echo_server_udp.cc)
add_executable(echo_client_udp sylar/examples/echo_client_udp.cc)
add_executable(test_ws_server tests/test_ws_server.cc)
add_executable(test_ws_client tests/test_ws_client.cc)
add_executable(chat_room sylar/examples/chat/chat_main.cc sylar/examples/chat/protocol.cc sylar/examples/chat/resource_servlet.cc sylar/examples/chat/chat_servlet.cc)

# === Protobuf ===
find_package(Protobuf REQUIRED)

# DDT 游戏微服务 (gate/login/lobby/battle/data)
add_subdirectory(src)


# === RPC framework (rpc_test 用的 test.pb；rpcheader.pb 已编入 libsylar) ===

add_custom_command(
    OUTPUT ${RPC_PB_DIR}/test.pb.cc ${RPC_PB_DIR}/test.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --cpp_out=${RPC_PB_DIR}
        --proto_path=${CMAKE_SOURCE_DIR}/tests/rpc_test
        ${CMAKE_SOURCE_DIR}/tests/rpc_test/test.proto
    DEPENDS ${CMAKE_SOURCE_DIR}/tests/rpc_test/test.proto
    COMMENT "Generating RPC test protobuf"
)

add_custom_target(rpc_pb DEPENDS
    ${RPC_PB_DIR}/rpcheader.pb.cc ${RPC_PB_DIR}/rpcheader.pb.h
    ${RPC_PB_DIR}/test.pb.cc ${RPC_PB_DIR}/test.pb.h
)

include_directories(${RPC_PB_DIR})

# === etcd-cpp-apiv3（C++17 + gRPC）：仅用于 RPC 模块的 etcd_client.cc ===
find_package(etcd-cpp-api CONFIG QUIET)
if(etcd-cpp-api_FOUND)
    message(STATUS "etcd-cpp-apiv3: found (target etcd-cpp-api)")
else()
    message(WARNING
        "etcd-cpp-apiv3 NOT found. Install it (needs gRPC+protobuf+cpprestsdk) before building RPC targets:\n"
        "  apt: libgrpc++-dev libgrpc-dev libprotobuf-dev protobuf-compiler-grpc && build etcd-cpp-apiv3 from source\n"
        "  vcpkg: vcpkg install etcd-cpp-apiv3\n"
        "See https://github.com/etcd-cpp-apiv3/etcd-cpp-apiv3")
endif()

# rpc_test_* 不再直接编译 RPC_SRC —— RPC 符号已随 libsylar 提供（避免重复定义）。
add_executable(rpc_test_callee
    tests/rpc_test/rpc_callee.cc
    ${RPC_PB_DIR}/test.pb.cc
    ${RPC_PB_DIR}/rpcheader.pb.cc
)
add_dependencies(rpc_test_callee rpc_pb sylar)
target_link_libraries(rpc_test_callee ${DEPS_LIBS} ${Protobuf_LIBRARIES})

add_executable(rpc_test_caller
    tests/rpc_test/rpc_caller.cc
    ${RPC_PB_DIR}/test.pb.cc
    ${RPC_PB_DIR}/rpcheader.pb.cc
)
add_dependencies(rpc_test_caller rpc_pb sylar)
target_link_libraries(rpc_test_caller ${DEPS_LIBS} ${Protobuf_LIBRARIES})


# 链接库
force_redefine_file_macro_for_sources(test) #__FILE__
target_link_libraries(test sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_config) #__FILE__
target_link_libraries(test_config sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_thread) #__FILE__
target_link_libraries(test_thread sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_util) #__FILE__
target_link_libraries(test_util sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_fiber) #__FILE__
target_link_libraries(test_fiber sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_scheduler) #__FILE__
target_link_libraries(test_scheduler sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_iomanager) #__FILE__
target_link_libraries(test_iomanager sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_timer) #__FILE__
target_link_libraries(test_timer sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_timewheel) #__FILE__
target_link_libraries(test_timewheel sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_hook) #__FILE__
target_link_libraries(test_hook sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_address) #__FILE__
target_link_libraries(test_address sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_socket) #__FILE__
target_link_libraries(test_socket sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_bytearray) #__FILE__
target_link_libraries(test_bytearray sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_http) #__FILE__
target_link_libraries(test_http sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_http_parser) #__FILE__
target_link_libraries(test_http_parser sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_tcp_server) #__FILE__
target_link_libraries(test_tcp_server sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(echo_server) #__FILE__
target_link_libraries(echo_server sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_http_server) #__FILE__
target_link_libraries(test_http_server sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_http_connection) #__FILE__
target_link_libraries(test_http_connection sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_uri) #__FILE__
target_link_libraries(test_uri sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(my_http_server) #__FILE__
target_link_libraries(my_http_server sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_daemon) #__FILE__
target_link_libraries(test_daemon sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_env) #__FILE__
target_link_libraries(test_env sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_asynclog) #__FILE__
target_link_libraries(test_asynclog sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_orm) #__FILE__
target_link_libraries(test_orm sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_etcd) #__FILE__
target_link_libraries(test_etcd sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(echo_server_udp) #__FILE__
target_link_libraries(echo_server_udp sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(echo_client_udp) #__FILE__
target_link_libraries(echo_client_udp sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_ws_server) #__FILE__
target_link_libraries(test_ws_server sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(test_ws_client) #__FILE__
target_link_libraries(test_ws_client sylar ${DEPS_LIBS})

force_redefine_file_macro_for_sources(chat_room) #__FILE__
target_link_libraries(chat_room sylar ${DEPS_LIBS})

# 输出路径
SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)