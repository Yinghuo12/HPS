#!/bin/bash
# DDT 弹弹堂 - 发布版打包脚本
# 用法: ./release.sh [macos|linux|all]
#   macos  - 打包 macOS 发布版（源码，目标机器编译）
#   linux  - 打包 Linux 发布版（源码，目标机器编译）
#   all    - 打包两个平台（默认）

set -e
cd "$(dirname "$0")/.."

BASE="$(pwd)"
SRC="$BASE/ddt_client"
PLATFORM=${1:-all}

build_release() {
    local SUFFIX="$1"
    local OUTDIR="$BASE/dist/ddt_client_$SUFFIX"

    echo "=== Building $SUFFIX release ==="
    rm -rf "$OUTDIR"
    mkdir -p "$OUTDIR/src/cmake" "$OUTDIR/proto" "$OUTDIR/thirdparty"

    # CMake 模块
    if [ -d "$BASE/ddt_client/cmake" ]; then
        cp "$BASE/ddt_client/cmake/"*.cmake "$OUTDIR/src/cmake/" 2>/dev/null || true
    fi

    # 源码（flat into src/）
    for f in main.cc game.h game.cc game_object.h game_object.cc \
             game_hud.h game_hud.cc game_network.h game_network.cc \
             game_renderer.h game_renderer.cc \
             shader.h shader.cc texture.h texture.cc \
             resource_manager.h resource_manager.cc \
             sprite_renderer.h sprite_renderer.cc \
             sprite_batch.h sprite_batch.cc \
             terrain.h terrain.cc projectile.h projectile.cc \
             text_renderer.h text_renderer.cc \
             camera.h camera.cc \
             particle_system.h particle_system.cc; do
        found=false
        for subdir in logic render battle network; do
            if [ -f "$SRC/$subdir/$f" ]; then
                cp "$SRC/$subdir/$f" "$OUTDIR/src/"
                found=true
                break
            fi
        done
        if ! $found && [ -f "$SRC/$f" ]; then
            cp "$SRC/$f" "$OUTDIR/src/"
        fi
    done

    # 网络层
    if [ -f "$SRC/network/ws_client.h" ]; then
        cp "$SRC/network/ws_client.h"  "$OUTDIR/src/ws_client.h"
        cp "$SRC/network/ws_client.cc" "$OUTDIR/src/ws_client.cc"
    fi
    if [ -f "$SRC/network/network_client_portable.h" ]; then
        cp "$SRC/network/network_client_portable.h"  "$OUTDIR/src/network_client.h"
        cp "$SRC/network/network_client_portable.cc" "$OUTDIR/src/network_client.cc"
        sed -i 's/network_client_portable\.h/network_client.h/g' "$OUTDIR/src/network_client.cc"
    elif [ -f "$SRC/network/network_client.h" ]; then
        cp "$SRC/network/network_client.h"  "$OUTDIR/src/network_client.h"
        cp "$SRC/network/network_client.cc" "$OUTDIR/src/network_client.cc"
    fi

    # 共享代码层 (common)
    if [ -d "$BASE/common" ]; then
        mkdir -p "$OUTDIR/src/common"
        cp "$BASE/common/"*.h "$OUTDIR/src/common/"
        cp "$BASE/common/"*.cc "$OUTDIR/src/common/"
    fi

    # 第三方库
    cp -r "$BASE/thirdparty/glad"  "$OUTDIR/thirdparty/"
    cp -r "$BASE/thirdparty/stb"   "$OUTDIR/thirdparty/"
    cp -r "$BASE/thirdparty/imgui" "$OUTDIR/thirdparty/"
    cp -r "$BASE/thirdparty/glfw"  "$OUTDIR/thirdparty/" 2>/dev/null || true
    cp -r "$BASE/thirdparty/glm"   "$OUTDIR/thirdparty/" 2>/dev/null || true
    cp -r "$BASE/thirdparty/protobuf_src" "$OUTDIR/thirdparty/"
    rm -rf "$OUTDIR/thirdparty/glfw/.git" "$OUTDIR/thirdparty/glm/.git" \
           "$OUTDIR/thirdparty/protobuf_src/.git" 2>/dev/null || true

    # FreeType
    if [ ! -d "$BASE/thirdparty/freetype" ]; then
        echo "Downloading FreeType source..."
        curl -sL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz | tar xz -C /tmp
        mv /tmp/freetype-VER-2-13-3 "$BASE/thirdparty/freetype"
    fi
    cp -r "$BASE/thirdparty/freetype" "$OUTDIR/thirdparty/"

    # 字体 & shaders & 游戏贴图
    mkdir -p "$OUTDIR/assets/fonts"
    cp "$BASE/ddt_client/assets/fonts/"* "$OUTDIR/assets/fonts/" 2>/dev/null || true
    # 复制所有游戏贴图 PNG
    for png in "$BASE/ddt_client/assets/"*.png; do
        [ -f "$png" ] && cp "$png" "$OUTDIR/assets/"
    done
    echo "  Assets: $(ls "$OUTDIR/assets/"*.png 2>/dev/null | wc -l) PNG files"
    if [ -d "$BASE/ddt_client/shaders" ]; then
        cp -r "$BASE/ddt_client/shaders" "$OUTDIR/shaders"
    fi

    # Proto source
    cp "$BASE/proto/ddt.proto" "$OUTDIR/proto/"

    # CMakeLists（standalone 版本）
    if [ -f "$BASE/ddt_client/CMakeLists.txt.standalone" ]; then
        cp "$BASE/ddt_client/CMakeLists.txt.standalone" "$OUTDIR/CMakeLists.txt"
    fi

    # 启动脚本
    cat > "$OUTDIR/ddt.sh" << 'LAUNCHER'
#!/bin/bash
# DDT 弹弹堂 - 一键构建运行
set -e
cd "$(dirname "$0")"

check_deps() {
    local missing=""
    for cmd in cmake make; do
        if ! command -v $cmd &>/dev/null; then
            missing="$missing $cmd"
        fi
    done
    if [ -n "$missing" ]; then
        echo "ERROR: missing build tools:$missing"
        if [[ "$OSTYPE" == "darwin"* ]]; then
            echo "Install:  xcode-select --install"
        elif [[ "$OSTYPE" == "linux"* ]]; then
            echo "Install:  sudo apt install build-essential cmake protobuf-compiler"
        fi
        exit 1
    fi
}

if [ ! -f "ddt_client" ]; then
    check_deps
    echo "Building ddt_client..."
    mkdir -p build && cd build
    if ! cmake .. -DCMAKE_BUILD_TYPE=Release 2>&1; then
        echo ""
        echo "=== cmake failed ==="
        echo "Common fixes:"
        echo "  macOS: xcode-select --install && brew install glfw freetype"
        echo "  Linux: sudo apt install build-essential cmake libglfw3-dev libfreetype-dev"
        exit 1
    fi
    JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    if ! make -j${JOBS} 2>&1; then
        echo "=== make failed ==="
        exit 1
    fi
    cd ..
    if [ -f build/ddt_client ]; then
        cp build/ddt_client .
    fi
    if [ ! -f "ddt_client" ]; then
        echo "Build failed"; exit 1
    fi
    echo "Build complete."
fi

if [[ "$OSTYPE" == "darwin"* ]] && [ ! -d "DDT.app" ]; then
    echo "Creating DDT.app..."
    mkdir -p DDT.app/Contents/MacOS
    mkdir -p DDT.app/Contents/Resources
    cp ddt_client DDT.app/Contents/MacOS/
    cat > DDT.app/Contents/Info.plist << 'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>ddt_client</string>
    <key>CFBundleIdentifier</key>
    <string>com.ddt.game</string>
    <key>CFBundleName</key>
    <string>DDT</string>
    <key>CFBundleDisplayName</key>
    <string>弹弹堂</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>LSMinimumSystemVersion</key>
    <string>11.0</string>
    <key>NSHighResolutionCapable</key>
    <true/>
</dict>
</plist>
PLIST
    chmod +x DDT.app/Contents/MacOS/ddt_client
    echo "Created DDT.app — double-click to run"
fi

if [[ "$OSTYPE" == "linux"* ]] && [ ! -f "DDT.desktop" ]; then
    echo "Creating DDT.desktop..."
    ABSPath="$(cd "$(dirname "$0")" && pwd)/ddt_client"
    cat > DDT.desktop << DESKTOP
[Desktop Entry]
Type=Application
Name=DDT
Comment=弹弹堂
Exec=$ABSPath
Icon=applications-games
Terminal=false
Categories=Game;
DESKTOP
    chmod +x DDT.desktop
    echo "Created DDT.desktop"
fi

echo "Starting DDT..."
cd "$(dirname "$0")"
exec ./ddt_client
LAUNCHER
    chmod +x "$OUTDIR/ddt.sh"

    # 打包
    mkdir -p "$BASE/dist"
    cd "$BASE/dist"
    local ARCHIVE="ddt_client_${SUFFIX}.tar.gz"
    rm -f "$ARCHIVE"
    tar czf "$ARCHIVE" "ddt_client_$SUFFIX/"
    local SIZE=$(ls -lh "$ARCHIVE" | awk '{print $5}')
    echo "Done: dist/$ARCHIVE ($SIZE)"
    echo ""
}

case "$PLATFORM" in
    macos) build_release macos ;;
    linux) build_release linux ;;
    all)   build_release macos; build_release linux ;;
    *)     echo "Usage: $0 [macos|linux|all]"; exit 1 ;;
esac

echo "=== All done ==="
echo ""
echo "macOS:  tar xzf ddt_client_macos.tar.gz && cd ddt_client_macos && ./ddt.sh"
echo "Linux:  tar xzf ddt_client_linux.tar.gz && cd ddt_client_linux && ./ddt.sh"
