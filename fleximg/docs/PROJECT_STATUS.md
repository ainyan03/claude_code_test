# プロジェクトステータス

## 🎯 現在の状態

**ステータス**: ✅ 安定版

ノードグラフエディタによる画像処理パイプライン構築ツールです。C++で実装された画像処理コアをWebAssemblyにコンパイルし、ブラウザ上で動作します。

## ✨ 主な機能

### ノードグラフエディタ
- **5種類のノードタイプ**: 画像、アフィン変換、合成、フィルタ、出力
- **ビジュアル接続**: ドラッグ&ドロップでノードを配置・接続
- **リアルタイムプレビュー**: パラメータ変更が即座に反映

### 画像処理
- アフィン変換（平行移動、回転、スケール）
- アルファブレンディング（画像合成）
- フィルタ処理（明るさ、コントラスト、グレースケール）

### UI/UX
- 左サイドバーメニュー（画像ライブラリ・出力設定）
- リサイズ可能なスプリッター
- プレビュー倍率調整（1x〜5x）
- レスポンシブデザイン（PC/モバイル対応）

## 🚀 デプロイ

- **URL**: https://ainyan03.github.io/claude_code_test/
- **自動デプロイ**: `main` または `claude/*` ブランチへのpushで実行
- **ビルド時間**: 約3-5分

## 🏗️ プロジェクト構造

```
fleximg/
├── src/fleximg/                   # C++コアライブラリ（Arduino互換構造）
│   ├── image_types.h              # 基本型定義（Image, AffineMatrix）
│   ├── image_allocator.h          # メモリアロケータ（組込み環境対応）
│   ├── pixel_format.h             # ピクセルフォーマット定義
│   ├── pixel_format_registry.h/cpp # フォーマット変換レジストリ
│   ├── image_buffer.h/cpp         # メモリ所有画像（RAII）
│   ├── viewport.h/cpp             # 純粋ビュー（軽量、所有権なし）
│   ├── eval_result.h              # パイプライン評価結果
│   ├── operators.h/cpp            # ノードオペレーター群
│   ├── evaluation_node.h/cpp      # パイプライン評価ノード
│   └── node_graph.h/cpp           # ノードグラフ評価エンジン
├── demo/                          # デモアプリケーション
│   ├── bindings.cpp               # Emscriptenバインディング
│   └── web/                       # Webフロントエンド
├── cli/                           # CLIツール（ネイティブビルド）
├── test/                          # ユニットテスト（Google Test）
├── third_party/                   # サードパーティライブラリ
├── docs/                          # ドキュメント（このフォルダ）
├── build.sh                       # WASMビルドスクリプト
├── Makefile                       # ネイティブビルド設定
└── README.md                      # プロジェクト概要
```

## 📦 ビルド

```bash
# WebAssemblyビルド（Emscripten環境が必要）
cd fleximg
./build.sh

# ネイティブビルド（テスト用）
make test
```

詳細は [README.md](../README.md) を参照してください。

## 🔗 関連ドキュメント

- [README.md](../README.md): プロジェクト概要と使い方
- [QUICKSTART.md](QUICKSTART.md): クイックスタートガイド
- [CHANGELOG.md](../CHANGELOG.md): 変更履歴
- [TODO.md](../TODO.md): 今後の機能拡張計画

### 設計ドキュメント

- [DESIGN_NODE_OPERATOR.md](DESIGN_NODE_OPERATOR.md): ノードオペレーター設計
- [DESIGN_PIPELINE_EVALUATION.md](DESIGN_PIPELINE_EVALUATION.md): パイプライン評価システム設計
- [DESIGN_TILE_COORDINATE_SYSTEM.md](DESIGN_TILE_COORDINATE_SYSTEM.md): タイル座標系設計
- [DESIGN_VIEWPORT_REFACTOR.md](DESIGN_VIEWPORT_REFACTOR.md): ViewPort構造リファクタリング設計
