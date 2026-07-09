#!/bin/bash
# DDT 弹弹堂 - 统一构建脚本
# 用法: ./build.sh [server|client|release|all|rpc|clean|setup]
#   setup   - 安装系统依赖（首次构建前执行）
#   server  - 构建服务端 (默认)
#   client  - 构建客户端 (需图形环境)
#   release - 打包客户端发布版
#   all     - 构建服务端 + 客户端
#   rpc     - 构建 RPC 测试
#   clean   - 清理构建

set -e
cd "$(dirname "$0")/../.."

ACTION=${1:-server}
BUILD_DIR="build"

gen_proto() {
    mkdir -p "$BUILD_DIR/proto"
    if [ ! -f "$BUILD_DIR/proto/ddt.pb.cc" ]; then
        protoc --proto_path=src/proto --cpp_out="$BUILD_DIR/proto" src/proto/ddt.proto
        echo "Generated protobuf"
    fi
}

case "$ACTION" in
    setup)
        echo "=== Installing system dependencies ==="
        if [[ "$OSTYPE" == "linux"* ]]; then
            sudo apt update
            sudo apt install -y build-essential cmake protobuf-compiler \
                libssl-dev libmysqlclient-dev libhiredis-dev \
                libzookeeper-mt-dev \
                libxinerama-dev libxcursor-dev libxi-dev libxrandr-dev
            echo ""
            echo "Done. Now run: bash packaging/scripts/build.sh all"
        elif [[ "$OSTYPE" == "darwin"* ]]; then
            xcode-select --install 2>/dev/null || true
            brew install cmake protobuf openssl mysql-client hiredis
            echo ""
            echo "Done. Now run: bash packaging/scripts/build.sh all"
        else
            echo "Unsupported OS: $OSTYPE"
            exit 1
        fi
        ;;
    clean)
        echo "Cleaning..."
        rm -rf "$BUILD_DIR" bin/ddt_server bin/ddt_client
        echo "Done."
        exit 0
        ;;
    server)
        echo "=== Building ddt_server ==="
        gen_proto
        mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
        cmake .. 2>&1 | tail -5
        make ddt_server -j$(nproc) 2>&1 | tail -3
        echo "Done: bin/ddt_server"
        ;;
    client)
        echo "=== Building ddt_client ==="
        gen_proto
        mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
        cmake .. 2>&1 | tail -5
        make ddt_client -j$(nproc) 2>&1 | tail -3
        echo "Done: bin/ddt_client"
        ;;
    release)
        echo "=== Building release package ==="
        bash packaging/scripts/release.sh
        ;;
    rpc)
        echo "=== Building RPC test ==="
        gen_proto
        mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
        cmake .. 2>&1 | tail -5
        make rpc_test_callee rpc_test_caller -j$(nproc) 2>&1 | tail -5
        echo "Done: bin/rpc_test_callee + bin/rpc_test_caller"
        ;;
    all)
        echo "=== Building all ==="
        gen_proto
        mkdir -p "$BUILD_DIR" && cd "$BUILD_DIR"
        cmake .. 2>&1 | tail -5
        make ddt_server ddt_client -j$(nproc) 2>&1 | tail -3
        echo "Done: bin/ddt_server + bin/ddt_client"
        ;;
    *)
        echo "Usage: $0 [setup|server|client|release|all|rpc|clean]"
        exit 1
        ;;
esac
