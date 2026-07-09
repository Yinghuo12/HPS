# HTTP / WebSocket 层：协议解析(Ragel) / 连接 / 会话 / Servlet / Server
sylar_add_sources(
    http.cc
    http11_parser.rl.cc
    httpclient_parser.rl.cc
    http_parser.cc
    http_connection.cc
    http_server.cc
    http_session.cc
    servlet.cc
    ws_connection.cc
    ws_server.cc
    ws_servlet.cc
    ws_session.cc
)
