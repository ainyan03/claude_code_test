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
  - 出力ノード: 最終結果のプレビュー
- **ノード操作**:
  - ドラッグ&ドロップでノードを配置
  - ノード間をワイヤーで接続
  - コンテキストメニューでノード追加・削除
- **デバッグ機能**:
  - タイル分割モード（矩形・スキャンライン）
  - チェッカーボードスキップ（タイル境界可視化）

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
- `image_transform.js`
- `image_transform.wasm`

### 3. アプリケーションの起動

```bash
cd demo/web
python3 -m http.server 8000
```

ブラウザで `http://localhost:8000` を開いてください。

## プロジェクト構造

```
fleximg/
├── src/fleximg/                  # C++コアライブラリ
│   ├── common.h                  # 共通定義
│   ├── render_types.h            # レンダリング型
│   ├── viewport.h/cpp            # 画像バッファ
│   ├── node.h                    # ノード基底クラス
│   ├── nodes/                    # ノード定義
│   │   ├── source_node.h         # 画像ソース
│   │   ├── sink_node.h           # 出力先
│   │   ├── transform_node.h      # アフィン変換
│   │   ├── filter_node.h         # フィルタ
│   │   ├── composite_node.h      # 合成
│   │   └── renderer_node.h       # パイプライン実行（発火点）
│   └── operations/               # 操作実装
│       ├── transform.h/cpp       # アフィン変換
│       ├── filters.h/cpp         # フィルタ処理
│       └── blend.h/cpp           # ブレンド処理
├── demo/                         # デモアプリケーション
│   ├── bindings.cpp              # WASMバインディング
│   └── web/                      # Webフロントエンド
│       ├── index.html            # メインHTML
│       ├── app.js                # JavaScript制御
│       ├── image_transform.js    # WebAssemblyラッパー
│       └── image_transform.wasm  # WebAssemblyバイナリ
├── test/                         # ユニットテスト
├── build.sh                      # WASMビルドスクリプト
└── README.md                     # このファイル
```

## 技術詳細

### アーキテクチャ

- **Node/Port モデル**: オブジェクト参照による直接接続
- **ViewPort/ImageBuffer 分離**: メモリ所有と参照の明確な分離
- **タイルベースレンダリング**: メモリ効率の良いタイル分割処理
- **RendererNode**: パイプライン実行の発火点（上流はプル型、下流はプッシュ型）

### 座標系

- **基準点相対座標**: 全ての座標は基準点（origin）からの相対位置
- **RenderRequest**: タイル要求（width, height, originX, originY）
- **RenderResult**: 評価結果（ImageBuffer + origin）

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
2. `#include "fleximg/common.h"` でインクルード
3. ノードを作成し、RendererNode で実行

## 開発

### デバッグビルド

```bash
./build.sh --debug
```

`--debug` オプションで性能計測が有効になります。

## 注意事項

このリポジトリは**実験的なプロジェクト**です。個人的な学習・検証目的で開発しており、一般向けのコントリビュートは募集していません。

コードの参照や個人的な利用は自由ですが、Issue や Pull Request への対応は行っていません。
