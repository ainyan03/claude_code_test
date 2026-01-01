#!/bin/bash

# Image Transform Preview - Build Script
# このスクリプトはC++コードをWebAssemblyにコンパイルします

set -e

echo "🔨 Building Image Transform WebAssembly module..."

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
mkdir -p web

# WebAssemblyにコンパイル
emcc src/image_transform.cpp src/bindings.cpp \
    -o web/image_transform.js \
    -std=c++11 \
    -O3 \
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
echo "  - web/image_transform.js"
echo "  - web/image_transform.wasm"
echo ""
echo "🚀 To run the application:"
echo "  cd web"
echo "  python3 -m http.server 8000"
echo ""
echo "Then open http://localhost:8000 in your browser"
echo "Or use any other web server to serve the web/ directory"
