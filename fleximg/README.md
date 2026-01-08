# 🖼️ fleximg

ノードベースのグラフエディタで画像処理パイプラインを視覚的に構築できるWebアプリケーションです。C++で実装された画像処理コアをWebAssemblyにコンパイルし、ブラウザ上で動作します。

Arduino等の組込み環境への移植を容易にするため、コアライブラリは`src/fleximg/`に配置されたArduino互換構造になっています。

## 🚀 ライブデモ

**GitHub Pagesで公開中！ビルド不要で即座に試せます：**

👉 **[https://ainyan03.github.io/claude_code_test/](https://ainyan03.github.io/claude_code_test/)**

PC・スマートフォン両方から直接アクセス可能です。C++で実装されたWebAssembly版が動作します。

## ✨ 特徴

- **ノードグラフエディタ**: ドラッグ&ドロップで画像処理パイプラインを視覚的に構築
- **C++コア**: 画像変換処理はC++で実装され、組込み環境への移植が容易
- **WebAssembly**: ブラウザ上でネイティブ速度で動作
- **レスポンシブUI**: PC・スマートフォン両対応
- **リアルタイムプレビュー**: ノードパラメータ変更が即座に反映
- **直感的な操作**: ノード接続とスライダーで簡単にパラメータ調整

## 🎯 機能

### ノードグラフエディタ
- ✅ **画像ライブラリ**: 複数画像の読み込み・管理（ドラッグ&ドロップ対応）
- ✅ **ノードタイプ**:
  - 画像ノード: ライブラリから画像を選択
  - アフィン変換ノード: 平行移動、回転、スケール調整
  - 合成ノード: 複数画像のアルファブレンディング
  - フィルタノード: 明るさ・コントラスト調整
  - 出力ノード: 最終結果のプレビュー
- ✅ **ノード操作**:
  - ドラッグ&ドロップでノードを配置
  - ノード間をワイヤーで接続
  - コンテキストメニューでノード追加・削除
  - パラメータ/行列表示モード切り替え
- ✅ **画像処理**:
  - アフィン変換（平行移動、回転、スケール）
  - 透過度調整（0〜100%）
  - 画像合成（アルファブレンディング）
  - 明るさ・コントラスト調整
- ✅ 合成画像のダウンロード（PNG形式）

### UI機能
- ✅ ノードグラフエディタ（ビジュアルプログラミング）
- ✅ リアルタイムプレビュー
- ✅ カスタマイズ可能なキャンバスサイズ
- ✅ レスポンシブデザイン（モバイル対応）
- ✅ タッチ操作対応

## 📋 必要要件

### 開発環境
- **Emscripten SDK**: C++をWebAssemblyにコンパイルするために必要
- **C++11以上対応コンパイラ**
- **Webサーバー**: ローカルでテストする場合（Python、Node.jsなど）

### 実行環境
- **モダンなWebブラウザ**
  - Chrome/Edge 57+
  - Firefox 52+
  - Safari 11+
  - モバイルブラウザ（iOS Safari、Android Chrome）

## 🔧 セットアップ

### 1. Emscriptenのインストール

```bash
# Emscripten SDKをクローン
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk

# 最新版をインストール
./emsdk install latest
./emsdk activate latest

# 環境変数を設定
source ./emsdk_env.sh
```

### 2. プロジェクトのビルド

```bash
cd fleximg

# ビルドスクリプトを実行
./build.sh
```

ビルドが成功すると、`demo/web/`ディレクトリに以下のファイルが生成されます：
- `image_transform.js`
- `image_transform.wasm`

### 3. アプリケーションの起動

#### Python 3を使用
```bash
cd demo/web
python3 -m http.server 8000
```

#### Node.jsを使用
```bash
cd demo/web
npx http-server -p 8000
```

ブラウザで `http://localhost:8000` を開いてください。

## 📱 使い方

### 基本操作

> 💡 **ヒント**: 起動時に3種類のテストパターン画像（Checker、Target、Grid）が自動的に画像ライブラリに追加されます。アフィン変換や合成のテストにすぐ使えます。

1. **画像を追加**
   - 画像ライブラリの「+ 画像追加」ボタンをクリック
   - 画像ファイルを選択（複数選択可）
   - ドラッグ&ドロップでも追加可能

2. **ノードの追加**
   - グラフエリアを右クリックしてコンテキストメニューを表示
   - 追加したいノードタイプを選択:
     - 画像ノード: ライブラリの画像を選択
     - アフィン変換ノード: 画像に変換を適用
     - 合成ノード: 複数画像を合成
     - フィルタノード: 明るさ・コントラスト調整
     - 出力ノード: 最終結果を表示
   - ノードは自動的にマウス位置に配置されます

3. **ノードの接続**
   - 出力ポート（右側の青い点）をクリック
   - 入力ポート（左側の青い点）までドラッグ
   - ワイヤーで接続が作成されます
   - 接続を削除するには、ワイヤーを右クリック

4. **パラメータ調整**
   - 各ノードのスライダーを操作してパラメータを調整
   - アフィン変換ノード: X位置、Y位置、回転、スケールX/Y、透過度
   - フィルタノード: 明るさ、コントラスト
   - 「パラメータ/行列」ボタンで表示モードを切り替え可能

5. **ノードの移動・削除**
   - ノードをドラッグして位置を変更
   - ノードを右クリックして「削除」を選択

6. **合成画像の保存**
   - 「📥 合成画像をダウンロード」ボタンをクリック
   - PNG形式でダウンロード

### スマートフォンでの利用

1. PCでアプリケーションを起動
2. スマートフォンのブラウザでPCのIPアドレスにアクセス
   - 例: `http://192.168.1.100:8000`
3. タッチ操作でノードの追加・接続・パラメータ調整が可能
4. ロングタップでコンテキストメニューを表示

## 🏗️ プロジェクト構造

```
fleximg/
├── src/fleximg/                   # C++コアライブラリ（Arduino互換構造）
│   ├── image_types.h              # 基本型定義（Image, AffineMatrix）
│   ├── image_allocator.h          # メモリアロケータ（組込み環境対応）
│   ├── pixel_format.h             # ピクセルフォーマット定義
│   ├── pixel_format_registry.h/cpp # フォーマット変換レジストリ
│   ├── viewport.h/cpp             # 統一画像型（ViewPort）
│   ├── operators.h/cpp            # ノードオペレーター群
│   ├── evaluation_node.h/cpp      # パイプライン評価ノード
│   └── node_graph.h/cpp           # ノードグラフ評価エンジン
├── demo/                          # デモアプリケーション
│   ├── bindings.cpp               # Emscriptenバインディング
│   └── web/                       # Webフロントエンド
│       ├── index.html             # メインHTML
│       ├── app.js                 # JavaScript制御
│       ├── image_transform.js     # 生成されたWebAssemblyラッパー
│       └── image_transform.wasm   # 生成されたWebAssemblyバイナリ
├── cli/                           # CLIツール（ネイティブビルド用）
│   ├── main.cpp                   # コマンドラインツール
│   └── stb_impl.cpp               # stb ライブラリ実装
├── test/                          # ユニットテスト（Google Test）
│   ├── viewport_test.cpp          # ViewPort テスト
│   └── affine_mapping_test.cpp    # AffineOperator テスト
├── third_party/                   # サードパーティライブラリ
│   ├── stb_image.h                # 画像読み込み
│   └── stb_image_write.h          # 画像書き出し
├── docs/                          # ドキュメント
│   ├── QUICKSTART.md              # クイックスタートガイド
│   ├── PROJECT_STATUS.md          # プロジェクト状況
│   ├── ARCHITECTURE.md            # アーキテクチャ概要
│   ├── BRANCH_STRATEGY.md         # ブランチ戦略
│   └── GITHUB_PAGES_SETUP.md      # GitHub Pages設定
├── build.sh                       # WASMビルドスクリプト
├── Makefile                       # ネイティブビルド設定
├── CHANGELOG.md                   # 変更履歴
├── TODO.md                        # タスク管理
└── README.md                      # このファイル
```

## 🔬 技術詳細

### C++コア（組込み環境への移植可能）

モジュラー設計のC++コアは、標準C++のみを使用しており、以下の機能を提供します：

- **基本型**（`image_types.h`）:
  - Image構造体: 8bit RGBAピクセルデータ（API境界用）
  - AffineMatrix構造体: アフィン変換行列
- **統一画像型**（`viewport.h/cpp`）:
  - ViewPort: 任意ピクセルフォーマット対応の統一画像型
  - サブビュー機能（メモリコピーなしで部分領域を参照）
  - カスタムアロケータ対応（組込み環境向け）
- **ピクセルフォーマット**（`pixel_format.h`, `pixel_format_registry.h/cpp`）:
  - 拡張可能なフォーマットシステム
  - RGBA8_Straight / RGBA16_Premultiplied 対応
  - 自動フォーマット変換
- **オペレーターシステム**（`operators.h/cpp`）:
  - NodeOperator基底クラスによる統一インターフェース
  - BrightnessOperator, GrayscaleOperator, BoxBlurOperator, AlphaOperator
  - AffineOperator, CompositeOperator
  - ファクトリパターンによるオペレーター生成
- **ノードグラフエンジン**（`node_graph.h/cpp`）:
  - 3段階評価アーキテクチャ（事前準備→要求伝播→評価）
  - タイル分割処理対応（組込み環境向け）
  - メモ化による重複計算の回避

### WebAssemblyバインディング

Emscriptenの`embind`を使用してC++クラスをJavaScriptから呼び出し可能にしています。

## 🚀 組込み環境への移植

モジュラー設計のC++コアは、以下の特徴により組込み環境への移植が容易です：

- **依存関係なし**: 標準C++17のみを使用（OpenCVなど不要）
- **モジュラー設計**: 必要なモジュールだけを選択して使用可能
- **シンプルなAPI**: `NodeGraphEvaluator`クラスでノードグラフを評価
- **メモリ管理**: `ImageAllocator`インターフェースでカスタムアロケータに対応
- **タイル分割処理**: メモリ制約のある環境でも大きな画像を処理可能
- **拡張性**: 新しいオペレーターを簡単に追加可能

### 移植手順

1. `src/fleximg/`フォルダごとプロジェクトにコピー
   - Arduino環境: `libraries/fleximg/` にコピーするだけで使用可能
   - その他環境: インクルードパスに `src/` を追加
2. 必要に応じて`ImageAllocator`を継承したカスタムアロケータを実装
3. `#include "fleximg/node_graph.h"` でインクルードして使用

## 🛠️ 開発

### ビルドオプションのカスタマイズ

`build.sh`内のEmscriptenコンパイルオプション：

- `-O3`: 最適化レベル（速度優先）
- `-s ALLOW_MEMORY_GROWTH=1`: 動的メモリ確保
- `-s TOTAL_MEMORY=256MB`: 初期メモリサイズ
- `-s MAXIMUM_MEMORY=512MB`: 最大メモリサイズ

### デバッグビルド

```bash
# WebAssemblyデバッグビルド（性能計測有効）
./build.sh --debug

# ネイティブデバッグビルド
make all FLEXIMG_DEBUG=1
make test FLEXIMG_DEBUG=1
```

`FLEXIMG_DEBUG` を有効にすると、各処理の実行時間（Filter/Affine/Composite/Convert/Output）がブラウザコンソールに表示されます。リリースビルドでは計測コードが完全に除去されます。

## ⚠️ 注意事項

このリポジトリは**実験的なプロジェクト**です。個人的な学習・検証目的で開発しており、一般向けのコントリビュートは募集していません。

コードの参照や個人的な利用は自由ですが、Issue や Pull Request への対応は行っていません。

## 📞 トラブルシューティング

問題が発生した場合：

1. ブラウザのコンソールでエラーメッセージを確認
2. Emscriptenが正しくインストールされているか確認
3. ブラウザがWebAssemblyに対応しているか確認
