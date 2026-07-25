# 网络层：地址 / Socket / 流 / ByteArray / TcpServer / URI / AsyncSocketStream
# uri.rl.cc 为 Ragel 生成源（已提交为普通 .cc，无 ragel 构建步骤）
sylar_add_sources(
    address.cc
    socket.cc
    stream.cc
    socket_stream.cc
    bytearray.cc
    tcp_server.cc
    async_socket_stream.cc
    uri.rl.cc
)
