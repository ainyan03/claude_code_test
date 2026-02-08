# fleximg

ノードベースのグラフエディタで画像処理パイプラインを視覚的に構築できるWebアプリケーションです。C++で実装された画像処理コアをWebAssemblyにコンパイルし、ブラウザ上で動作します。

Arduino等の組込み環境への移植を容易にするため、コアライブラリはヘッダオンリーに近いシンプルな構造になっています。

## ライブデモ

**GitHub Pagesで公開中！ビルド不要で即座に試せます：**

https://ainyan03.github.io/claude_code_test/

PC・スマートフォン両方から直接アクセス可能です。C++で実装されたWebAssembly版が動作します。

## 特徴

- **ノードグラフエディタ**: ドラッグ&ドロップで画像処理パイプラインを視覚的に構築
- **C++コア**: 画像変換処理はC++で実装され、組込み環境への移植が容易
- **WebAssembly**: ブラウザ上でネイティブ速度で動作
- **レスポンシブUI**: PC・スマートフォン両対応
- **リアルタイムプレビュー**: ノードパラメータ変更が即座に反映
- **タイル分割レンダリング**: メモリ制約のある環境でも大きな画像を処理可能

## 機能

### ノードグラフエディタ
- **画像ライブラリ**: 複数画像の読み込み・管理（ドラッグ&ドロップ対応）
- **ノードタイプ**:
  - 画像ノード: ライブラリから画像を選択
  - アフィン変換ノード: 平行移動、回転、スケール調整
  - 合成ノード: 複数画像のアルファブレンディング
  - フィルタノード: 明るさ、グレースケール、ぼかし、アルファ
  - Renderer/Sinkノード: パイプライン実行と出力管理
- **ノード操作**:
  - ドラッグ&ドロップでノードを配置
  - ノード間をワイヤーで接続
  - コンテキストメニューでノード追加・削除
- **デバッグ機能**:
  - タイル分割モード（矩形・スキャンライン）
  - チェッカーボードスキップ（タイル境界可視化）

## ドキュメント

### 初めての方へ（推奨する読む順番）

1. **[QUICKSTART.md](docs/QUICKSTART.md)** - 5分で動作確認（ライブデモの操作方法）
2. **[ARCHITECTURE.md](docs/ARCHITECTURE.md)** - アーキテクチャ概要（15分で全体像を把握）
3. **本ファイルの「組込み環境への移植」セクション** - 自分のプロジェクトへの組み込み方

### 深く学びたい方へ

| ドキュメント | 内容 |
|-------------|------|
| [DESIGN_RENDERER_NODE.md](docs/DESIGN_RENDERER_NODE.md) | パイプライン実行の発火点、Pull/Push処理フロー |
| [DESIGN_FILTER_NODES.md](docs/DESIGN_FILTER_NODES.md) | フィルタノードの階層構造、新規フィルタ追加方法 |
| [DESIGN_TYPE_STRUCTURE.md](docs/DESIGN_TYPE_STRUCTURE.md) | ViewPort/ImageBuffer/RenderResponseの設計 |
| [DESIGN_PIXEL_FORMAT.md](docs/DESIGN_PIXEL_FORMAT.md) | ピクセルフォーマット変換アルゴリズム |

全てのドキュメントは [docs/README.md](docs/README.md) から参照できます。

### 変更履歴

| ドキュメント | 内容 |
|-------------|------|
| [CHANGELOG.md](CHANGELOG.md) | 最新の変更履歴（v2.62以降の要約） |
| [docs/CHANGELOG_v2_detailed.md](docs/CHANGELOG_v2_detailed.md) | v2.0～v2.61の詳細な変更履歴 |

## 必要要件

### 開発環境
- **Emscripten SDK**: C++をWebAssemblyにコンパイルするために必要
- **C++17以上対応コンパイラ**
- **Webサーバー**: ローカルでテストする場合（Python、Node.jsなど）

### 実行環境
- **モダンなWebブラウザ**
  - Chrome/Edge 57+
  - Firefox 52+
  - Safari 11+
  - モバイルブラウザ（iOS Safari、Android Chrome）

## セットアップ

### 1. Emscriptenのインストール

```bash
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh
```

### 2. プロジェクトのビルド

```bash
cd fleximg
./build.sh
```

ビルドが成功すると、`demo/web/`ディレクトリに以下のファイルが生成されます：
- `fleximg.js`
- `fleximg.wasm`

### 3. アプリケーションの起動

```bash
cd demo/web
python3 server.py
```

ブラウザで `http://localhost:8080` を開いてください。

> **Note**: `server.py` はCross-Origin Isolationヘッダ（COOP/COEP）を付与し、`performance.now()` の高精度タイマーを有効にします。

## プロジェクト構造

```
fleximg/
├── examples/                     # 実機サンプル
│   └── m5stack_basic/            # M5Stack回転矩形デモ
├── src/fleximg/                  # C++コアライブラリ
│   ├── common.h                  # 共通定義
│   ├── render_types.h            # レンダリング型
│   ├── viewport.h                # 画像バッファ
│   ├── node.h                    # ノード基底クラス
│   ├── nodes/                    # ノード定義
│   │   ├── source_node.h         # 画像ソース
│   │   ├── sink_node.h           # 出力先
│   │   ├── affine_node.h         # アフィン変換
│   │   ├── filter_node_base.h    # フィルタ共通基底
│   │   ├── brightness_node.h     # 明るさ調整
│   │   ├── grayscale_node.h      # グレースケール
│   │   ├── horizontal_blur_node.h # 水平ぼかし
│   │   ├── vertical_blur_node.h  # 垂直ぼかし
│   │   ├── alpha_node.h          # アルファ調整
│   │   ├── composite_node.h      # 合成
│   │   └── renderer_node.h       # パイプライン実行（発火点）
│   └── operations/               # 操作実装
│       ├── transform.h           # DDA範囲計算
│       ├── filters.h/cpp         # フィルタ処理
│       └── canvas_utils.h        # キャンバス操作
├── demo/                         # デモアプリケーション
│   ├── bindings.cpp              # WASMバインディング
│   └── web/                      # Webフロントエンド
│       ├── index.html            # メインHTML
│       ├── app.js                # JavaScript制御
│       ├── fleximg.js            # WebAssemblyラッパー
│       └── fleximg.wasm          # WebAssemblyバイナリ
├── test/                         # ユニットテスト
├── docs/                         # ドキュメント
│   ├── ARCHITECTURE.md           # アーキテクチャ概要
│   ├── DESIGN_*.md               # 設計ドキュメント
│   └── ideas/                    # 構想段階のアイデア
│       └── IDEA_*.md
├── build.sh                      # WASMビルドスクリプト
├── TODO.md                       # タスク管理
└── README.md                     # このファイル
```

### ドキュメント運用

| フォルダ/ファイル | 役割 | 内容 |
|------------------|------|------|
| `TODO.md` | タスク管理 | 簡潔なタスクリスト、詳細へのリンク |
| `docs/DESIGN_*.md` | 設計ドキュメント | 実装済み機能の設計説明 |
| `docs/ideas/IDEA_*.md` | 構想段階 | 未実装のアイデア・検討中の設計 |

**運用指針**:
- 構想段階のアイデアは `docs/ideas/` に詳細を記載し、`TODO.md` には概要とリンクのみ
- 実装が決まったら `docs/DESIGN_*.md` に昇格
- `TODO.md` は閲覧性を重視し、簡潔に保つ

## 技術詳細

### アーキテクチャ

- **Node/Port モデル**: オブジェクト参照による直接接続
- **ViewPort/ImageBuffer 分離**: メモリ所有と参照の明確な分離
- **タイルベースレンダリング**: メモリ効率の良いタイル分割処理
- **RendererNode**: パイプライン実行の発火点（上流はプル型、下流はプッシュ型）

### 座標系

- **基準点相対座標**: 全ての座標は基準点（origin）からの相対位置
- **RenderRequest**: タイル要求（width, height, originX, originY）
- **RenderResponse**: 評価結果（ImageBuffer + origin）

### ピクセルフォーマット

- **RGBA8_Straight**: 8bit/ch、ストレートアルファ（入出力用）
- **RGBA16_Premultiplied**: 16bit/ch、プリマルチアルファ（内部合成用）

## 組込み環境への移植

C++コアは以下の特徴により組込み環境への移植が容易です：

- **依存関係なし**: 標準C++17のみを使用
- **ヘッダオンリー設計**: ノード定義はヘッダのみ
- **シンプルなAPI**: RendererNode でパイプラインを実行
- **タイル分割処理**: メモリ制約のある環境でも大きな画像を処理可能

### 移植手順

1. `src/fleximg/`フォルダをプロジェクトにコピー
2. `#include "fleximg/core/common.h"` でインクルード
3. ノードを作成し、RendererNode で実行

### M5Stack サンプル

`examples/m5stack_basic/` に M5Stack Core2/CoreS3 向けのサンプルがあります。

```bash
# プロジェクトルートからビルド（platformio.ini参照）
pio run -e basic_m5stack_core2 -t upload  # Core2に書き込み
pio run -e basic_native                    # macOS/Linux/Windowsでテスト（SDL使用）
```

- LcdSinkNode: M5GFX経由でLCDにスキャンライン転送するカスタムSinkNode
- RGBA8_Straight → RGB565_BE変換
- 回転する矩形のデモ

## 開発

### デバッグビルド

```bash
./build.sh --debug
```

`--debug` オプションで性能計測が有効になります。

### ネイティブテスト

C++コアのユニットテストを実行できます（[doctest](https://github.com/doctest/doctest) 使用）。

```bash
cd test
make all_tests && ./all_tests    # 全テスト（91ケース）をビルド・実行
make clean                        # ビルド成果物を削除
```

特定のテストのみ実行：

```bash
./all_tests --test-case="*AffineNode*"     # AffineNode関連のみ
./all_tests --test-case="*blend*"          # blend関連のみ
./all_tests --list-test-cases              # テストケース一覧
```

個別のテストバイナリを作成：

```bash
make viewport_test && ./viewport_test
make blend_test && ./blend_test
```

## 注意事項

このリポジトリは**実験的なプロジェクト**です。個人的な学習・検証目的で開発しており、一般向けのコントリビュートは募集していません。

コードの参照や個人的な利用は自由ですが、Issue や Pull Request への対応は行っていません。
