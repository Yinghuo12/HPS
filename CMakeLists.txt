cmake_minimum_required(VERSION 3.5)
project(sylar)

include(cmake/utils.cmake)  # 引入强制定义宏，修改显示文件为相对路径


set(CMAKE_VERBOSE_MAKEFILE ON) # 打印详细编译信息
set(CMAKE_CXX_FLAGS "$ENV{CXXFLAGS} -rdynamic -O0 -ggdb -std=c++11 -Wall -Wno-deprecated -Werror -Wno-unused-function -Wno-builtin-macro-redefined")


# 头文件
include_directories(.)
include_directories(./include)
include_directories(./include/boost)

# 库目录
link_directories(./lib)


# 源文件
set(LIB_SRC
    sylar/log.cc
    sylar/util.cc
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
    sylar/http/http_connection.cc
    sylar/uri.rl.cc
    sylar/daemon.cc
    sylar/env.cc
    sylar/asynclog.cc  # 二次开发异步日志
)

# ragelmaker(sylar/http/http11_parser.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/sylar/http)
# ragelmaker(sylar/http/httpclient_parser.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/sylar/http)
# ragelmaker(sylar/uri.rl LIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/sylar)

# 编译库
add_library(sylar SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(sylar) #__FILE__


# 测试程序
add_executable(test tests/test.cc) 
add_executable(test_config tests/test_config.cc)
add_executable(test_thread tests/test_thread.cc)
add_executable(test_util tests/test_util.cc)
add_executable(test_fiber tests/test_fiber.cc)
add_executable(test_scheduler tests/test_scheduler.cc)
add_executable(test_iomanager tests/test_iomanager.cc)
add_executable(test_timer tests/test_timer.cc)
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
add_executable(my_http_server samples/my_http_server.cc)
add_executable(test_daemon tests/test_daemon.cc)
add_executable(test_env tests/test_env.cc)
add_executable(test_asynclog tests/test_asynclog.cc)  # 二次开发异步日志
add_executable(echo_server_udp sylar/examples/echo_server_udp.cc)
add_executable(echo_client_udp sylar/examples/echo_client_udp.cc)


# 链接库
force_redefine_file_macro_for_sources(test) #__FILE__
target_link_libraries(test sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_config) #__FILE__
target_link_libraries(test_config sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_thread) #__FILE__
target_link_libraries(test_thread sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_util) #__FILE__
target_link_libraries(test_util sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_fiber) #__FILE__
target_link_libraries(test_fiber sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_scheduler) #__FILE__
target_link_libraries(test_scheduler sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_iomanager) #__FILE__
target_link_libraries(test_iomanager sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_timer) #__FILE__
target_link_libraries(test_timer sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_hook) #__FILE__
target_link_libraries(test_hook sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_address) #__FILE__
target_link_libraries(test_address sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_socket) #__FILE__
target_link_libraries(test_socket sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_bytearray) #__FILE__
target_link_libraries(test_bytearray sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_http) #__FILE__
target_link_libraries(test_http sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_http_parser) #__FILE__
target_link_libraries(test_http_parser sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_tcp_server) #__FILE__
target_link_libraries(test_tcp_server sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(echo_server) #__FILE__
target_link_libraries(echo_server sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_http_server) #__FILE__
target_link_libraries(test_http_server sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_http_connection) #__FILE__
target_link_libraries(test_http_connection sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_uri) #__FILE__
target_link_libraries(test_uri sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(my_http_server) #__FILE__
target_link_libraries(my_http_server sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_daemon) #__FILE__
target_link_libraries(test_daemon sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_env) #__FILE__
target_link_libraries(test_env sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(test_asynclog) #__FILE__
target_link_libraries(test_asynclog sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(echo_server_udp) #__FILE__
target_link_libraries(echo_server_udp sylar -lyaml-cpp)

force_redefine_file_macro_for_sources(echo_client_udp) #__FILE__
target_link_libraries(echo_client_udp sylar -lyaml-cpp)

# 输出路径
SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)