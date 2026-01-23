#!/bin/bash

# fleximg Demo - WebAssembly Build Script
# このスクリプトはC++コードをWebAssemblyにコンパイルします
#
# Usage:
#   ./build.sh            リリースビルド（8bit Straight合成、省メモリ）
#   ./build.sh --debug    デバッグビルド（性能計測有効）
#   ./build.sh --premul   16bit Premul合成モード（高精度）

set -e

# オプション判定
DEBUG_MODE=0
PREMUL_MODE=0
DEBUG_FLAGS=""
PREMUL_FLAGS=""
RELEASE_FLAGS="-DNDEBUG"  # リリースビルドではassertを無効化
for arg in "$@"; do
    case $arg in
        --debug)
            DEBUG_MODE=1
            DEBUG_FLAGS="-DFLEXIMG_DEBUG"
            RELEASE_FLAGS=""  # デバッグビルドではassertを有効化
            ;;
        --premul)
            PREMUL_MODE=1
            PREMUL_FLAGS="-DFLEXIMG_ENABLE_PREMUL"
            ;;
    esac
done

# ビルドモード表示
BUILD_MODE=""
if [ $DEBUG_MODE -eq 1 ]; then
    BUILD_MODE="DEBUG"
fi
if [ $PREMUL_MODE -eq 1 ]; then
    if [ -n "$BUILD_MODE" ]; then
        BUILD_MODE="$BUILD_MODE + PREMUL"
    else
        BUILD_MODE="PREMUL"
    fi
fi
if [ -n "$BUILD_MODE" ]; then
    echo "🔨 Building fleximg WebAssembly demo ($BUILD_MODE mode)..."
else
    echo "🔨 Building fleximg WebAssembly demo..."
fi

# Emscriptenの確認
if ! command -v emcc &> /dev/null; then
    echo "❌ Error: Emscripten (emcc) not found!"
    echo "Please install Emscripten SDK:"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk"
    echo "  ./emsdk install latest"
    echo "  ./emsdk activate latest"
    echo "  source ./emsdk_env.sh"
    exit 1
fi

# 出力ディレクトリ作成
mkdir -p demo/web

# ビルド情報を生成
BUILD_DATE=$(date -u +"%Y-%m-%d %H:%M:%S UTC")
GIT_COMMIT=$(git rev-parse --short HEAD 2>/dev/null || echo "unknown")
GIT_BRANCH=$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")

# 合成モード判定
if [ $PREMUL_MODE -eq 1 ]; then
    COMPOSITE_MODE="16bit Premul"
else
    COMPOSITE_MODE="8bit Straight"
fi

echo "// Build information - auto-generated" > demo/web/version.js
echo "const BUILD_INFO = {" >> demo/web/version.js
echo "  buildDate: '$BUILD_DATE'," >> demo/web/version.js
echo "  gitCommit: '$GIT_COMMIT'," >> demo/web/version.js
echo "  gitBranch: '$GIT_BRANCH'," >> demo/web/version.js
echo "  backend: 'WebAssembly'," >> demo/web/version.js
echo "  compositeMode: '$COMPOSITE_MODE'" >> demo/web/version.js
echo "};" >> demo/web/version.js

echo "📝 Build info: $BUILD_DATE (commit: $GIT_COMMIT)"

# WebAssemblyにコンパイル
# Note: fleximg.cpp は stb方式で platform.h, viewport.h, pool_allocator.h, filters.h の実装を含む
emcc src/fleximg/fleximg.cpp \
     src/fleximg/image/pixel_format.cpp \
     demo/bindings.cpp \
    -I src \
    -o demo/web/fleximg.js \
    -std=c++17 \
    -Wall -Wextra -Wpedantic \
    -Wconversion -Wsign-conversion -Wshadow -Wcast-qual -Wdouble-promotion \
    -Wformat=2 -Wnull-dereference -Wunused \
    -O3 \
    $RELEASE_FLAGS \
    $DEBUG_FLAGS \
    $PREMUL_FLAGS \
    -s WASM=1 \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s MODULARIZE=1 \
    -s EXPORT_NAME="Module" \
    -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' \
    -s DISABLE_EXCEPTION_CATCHING=0 \
    --bind

echo "✅ Build complete!"
echo ""
echo "📦 Generated files:"
echo "  - demo/web/fleximg.js"
echo "  - demo/web/fleximg.wasm"
echo ""
echo "🚀 To run the demo:"
echo "  cd demo/web"
echo "  python3 -m http.server 8080"
echo ""
echo "Then open http://localhost:8080 in your browser"
echo "Or use any other web server to serve the demo/web/ directory"
