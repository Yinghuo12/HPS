#!/usr/bin/env bash
# 重新生成 C# proto 代码到 Proto/。proto 改动后跑这个。
#
# 需本机有 protoc(>=3.0, 支持 --csharp_out)。
#   Linux: apt install protobuf-compiler  或  https://github.com/protocolbuffers/protobuf/releases
#   macOS: brew install protobuf
set -e
cd "$(dirname "$0")"     # 进 src/client/
PROTO_DIR="../proto"      # 仓库 src/proto
OUT="Proto"
mkdir -p "$OUT"

protoc --csharp_out="$OUT" --proto_path="$PROTO_DIR" \
    "$PROTO_DIR/common.proto" \
    "$PROTO_DIR/gate.proto"

echo "generated:"
ls -la "$OUT"/Common.cs "$OUT"/Gate.cs
echo ""
echo "注意: 生成代码依赖 Google.Protobuf.dll, 把它放进 Unity Assets/Plugins/。"
