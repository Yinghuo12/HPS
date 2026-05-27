cmake_minimum_required(VERSION 3.14)
project(sylar)

include(FetchContent)
include(framework/cmake/utils.cmake)

set(CMAKE_VERBOSE_MAKEFILE ON)
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -ggdb -std=c++11 -Wall -Wno-deprecated -Werror -Wno-unused-function -Wno-builtin-macro-redefined -Wno-deprecated-declarations -Wno-pessimizing-move")

# === OpenSSL ===
find_package(OpenSSL REQUIRED)
include_directories(${OPENGL_INCLUDE_DIR})

# === 头文件 ===
include_directories(.)
include_directories(./framework/include)
include_directories(./framework/include/jsoncpp)

# === Boost (headers) ===
if(EXISTS "${CMAKE_SOURCE_DIR}/framework/include/boost/boost")
    include_directories(./framework/include/boost)
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
if(EXISTS "${CMAKE_SOURCE_DIR}/framework/include/yaml-cpp/yaml-cpp")
    include_directories(./framework/include/yaml-cpp)
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


# 源文件
set(LIB_SRC
    sylar/log.cc
    sylar/util.cc
    sylar/util/hash_util.cc
    sylar/config.cc
    sylar/thread.cc
    sylar/fiber.cc
    sylar/scheduler.cc
    sylar/iomanager.cc
    sylar/timer.cc
    sylar/hook.cc
    sylar/fd_manager.cc
    sylar/address.cc
    sylar/socket.cc
    sylar/bytearray.cc
    sylar/http/http.cc
    sylar/http/http11_parser.rl.cc
    sylar/http/httpclient_parser.rl.cc
    sylar/http/http_parser.cc
    sylar/tcp_server.cc
    sylar/stream.cc
    sylar/socket_stream.cc
    sylar/http/http_session.cc
    sylar/http/http_server.cc
    sylar/http/servlet.cc
    sylar/http/ws_connection.cc
    sylar/http/ws_server.cc
    sylar/http/ws_session.cc
    sylar/http/ws_servlet.cc
    sylar/http/http_connection.cc
    sylar/uri.rl.cc
    sylar/daemon.cc
    sylar/env.cc
    sylar/asynclog.cc  # 二次开发异步日志
    sylar/util/json_util.cc
)


# 编译库
add_library(sylar SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(sylar) #__FILE__

# 统一链接所有以来库
set(DEPS_LIBS
    sylar
    ${YAML_CPP_LINK}
    -lpthread
    -ldl
    -lssl
    -lcrypto
    -ljsoncpp
)



# 测试程序
add_executable(test framework/tests/test.cc)
add_executable(test_config framework/tests/test_config.cc)
add_executable(test_thread framework/tests/test_thread.cc)
add_executable(test_util framework/tests/test_util.cc)
add_executable(test_fiber framework/tests/test_fiber.cc)
add_executable(test_scheduler framework/tests/test_scheduler.cc)
add_executable(test_iomanager framework/tests/test_iomanager.cc)
add_executable(test_timer framework/tests/test_timer.cc)
add_executable(test_hook framework/tests/test_hook.cc)
add_executable(test_address framework/tests/test_address.cc)
add_executable(test_socket framework/tests/test_socket.cc)
add_executable(test_bytearray framework/tests/test_bytearray.cc)
add_executable(test_http framework/tests/test_http.cc)
add_executable(test_http_parser framework/tests/test_http_parser.cc)
add_executable(test_tcp_server framework/tests/test_tcp_server.cc)
add_executable(echo_server sylar/examples/echo_server.cc)
add_executable(test_http_server framework/tests/test_http_server.cc)
add_executable(test_http_connection framework/tests/test_http_connection.cc)
add_executable(test_uri framework/tests/test_uri.cc)
add_executable(my_http_server framework/samples/my_http_server.cc)
add_executable(test_daemon framework/tests/test_daemon.cc)
add_executable(test_env framework/tests/test_env.cc)
add_executable(test_asynclog framework/tests/test_asynclog.cc)  # 二次开发异步日志
add_executable(echo_server_udp sylar/examples/echo_server_udp.cc)
add_executable(echo_client_udp sylar/examples/echo_client_udp.cc)
add_executable(test_ws_server framework/tests/test_ws_server.cc)
add_executable(test_ws_client framework/tests/test_ws_client.cc)
add_executable(chat_room framework/chat/chat_main.cc framework/chat/protocol.cc framework/chat/resource_servlet.cc framework/chat/chat_servlet.cc)

# DDT 游戏客户端（独立构建，不使用框架的 -Werror）
add_subdirectory(ddt_client)

# === Protobuf for DDT game ===
find_package(Protobuf REQUIRED)
set(DDT_PROTO_DIR ${CMAKE_SOURCE_DIR}/proto)
set(DDT_PB_DIR ${CMAKE_BINARY_DIR}/proto)
file(MAKE_DIRECTORY ${DDT_PB_DIR})

add_custom_command(
    OUTPUT ${DDT_PB_DIR}/ddt.pb.cc ${DDT_PB_DIR}/ddt.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --cpp_out=${DDT_PB_DIR}
        --proto_path=${DDT_PROTO_DIR}
        ${DDT_PROTO_DIR}/ddt.proto
    DEPENDS ${DDT_PROTO_DIR}/ddt.proto
    COMMENT "Generating DDT protobuf sources"
)

add_custom_target(ddt_pb DEPENDS ${DDT_PB_DIR}/ddt.pb.cc ${DDT_PB_DIR}/ddt.pb.h)

# DDT 游戏服务器
add_subdirectory(ddt_server)


# === RPC framework ===
set(RPC_PB_DIR ${CMAKE_BINARY_DIR}/rpc)
file(MAKE_DIRECTORY ${RPC_PB_DIR})

add_custom_command(
    OUTPUT ${RPC_PB_DIR}/rpcheader.pb.cc ${RPC_PB_DIR}/rpcheader.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --cpp_out=${RPC_PB_DIR}
        --proto_path=${CMAKE_SOURCE_DIR}/sylar/rpc
        ${CMAKE_SOURCE_DIR}/sylar/rpc/rpcheader.proto
    DEPENDS ${CMAKE_SOURCE_DIR}/sylar/rpc/rpcheader.proto
    COMMENT "Generating RPC header protobuf"
)

add_custom_command(
    OUTPUT ${RPC_PB_DIR}/test.pb.cc ${RPC_PB_DIR}/test.pb.h
    COMMAND ${Protobuf_PROTOC_EXECUTABLE}
        --cpp_out=${RPC_PB_DIR}
        --proto_path=${CMAKE_SOURCE_DIR}/framework/tests/rpc_test
        ${CMAKE_SOURCE_DIR}/framework/tests/rpc_test/test.proto
    DEPENDS ${CMAKE_SOURCE_DIR}/framework/tests/rpc_test/test.proto
    COMMENT "Generating RPC test protobuf"
)

add_custom_target(rpc_pb DEPENDS
    ${RPC_PB_DIR}/rpcheader.pb.cc ${RPC_PB_DIR}/rpcheader.pb.h
    ${RPC_PB_DIR}/test.pb.cc ${RPC_PB_DIR}/test.pb.h
)

include_directories(${RPC_PB_DIR})

set(RPC_SRC
    ${CMAKE_SOURCE_DIR}/sylar/rpc/rpc_controller.cc
    ${CMAKE_SOURCE_DIR}/sylar/rpc/rpc_provider.cc
    ${CMAKE_SOURCE_DIR}/sylar/rpc/rpc_channel.cc
    ${CMAKE_SOURCE_DIR}/sylar/zk_client.cc
)

add_executable(rpc_test_callee
    framework/tests/rpc_test/rpc_callee.cc
    ${RPC_SRC}
    ${RPC_PB_DIR}/test.pb.cc
    ${RPC_PB_DIR}/rpcheader.pb.cc
)
add_dependencies(rpc_test_callee rpc_pb sylar)
target_link_libraries(rpc_test_callee ${DEPS_LIBS} ${Protobuf_LIBRARIES} -lzookeeper_mt)

add_executable(rpc_test_caller
    framework/tests/rpc_test/rpc_caller.cc
    ${RPC_SRC}
    ${RPC_PB_DIR}/test.pb.cc
    ${RPC_PB_DIR}/rpcheader.pb.cc
)
add_dependencies(rpc_test_caller rpc_pb sylar)
target_link_libraries(rpc_test_caller ${DEPS_LIBS} ${Protobuf_LIBRARIES} -lzookeeper_mt)


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