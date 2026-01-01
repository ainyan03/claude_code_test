# 🖼️ Image Transform Preview

複数の画像にアフィン変換を適用し、透過合成してプレビューできるWebアプリケーションです。C++で実装された画像処理コアをWebAssemblyにコンパイルし、ブラウザ上で動作します。

## ✨ 特徴

- **C++コア**: 画像変換処理はC++で実装され、組込み環境への移植が容易
- **WebAssembly**: ブラウザ上でネイティブ速度で動作
- **レスポンシブUI**: PC・スマートフォン両対応
- **リアルタイムプレビュー**: パラメータ変更が即座に反映
- **直感的な操作**: スライダーで簡単にパラメータ調整

## 🎯 機能

### 画像処理機能
- ✅ 複数画像の読み込み（ドラッグ&ドロップ対応）
- ✅ アフィン変換
  - 平行移動（X/Y方向）
  - 回転（0〜360度）
  - スケール（X/Y方向独立）
- ✅ 透過度調整（0〜100%）
- ✅ レイヤー順序変更
- ✅ レイヤー表示/非表示切り替え
- ✅ 合成画像のダウンロード（PNG形式）

### UI機能
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
cd image-transform-preview

# ビルドスクリプトを実行
./build.sh
```

ビルドが成功すると、`web/`ディレクトリに以下のファイルが生成されます：
- `image_transform.js`
- `image_transform.wasm`

### 3. アプリケーションの起動

#### Python 3を使用
```bash
cd web
python3 -m http.server 8000
```

#### Node.jsを使用
```bash
cd web
npx http-server -p 8000
```

ブラウザで `http://localhost:8000` を開いてください。

## 📱 使い方

### 基本操作

1. **画像を追加**
   - 「+ 画像追加」ボタンをクリック
   - 画像ファイルを選択（複数選択可）

2. **パラメータ調整**
   - 各レイヤーのスライダーを操作
   - X位置/Y位置: 画像の位置を移動
   - 回転: 画像を回転（0〜360度）
   - スケールX/Y: 画像の拡大縮小
   - 透過度: 画像の不透明度を調整

3. **レイヤー管理**
   - ✓ チェックボックス: レイヤーの表示/非表示
   - ↑/↓ ボタン: レイヤーの順序変更
   - × ボタン: レイヤーを削除

4. **合成画像の保存**
   - 「📥 合成画像をダウンロード」ボタンをクリック
   - PNG形式でダウンロード

### スマートフォンでの利用

1. PCでアプリケーションを起動
2. スマートフォンのブラウザでPCのIPアドレスにアクセス
   - 例: `http://192.168.1.100:8000`
3. タッチ操作でスライダーを調整可能

## 🏗️ プロジェクト構造

```
image-transform-preview/
├── src/
│   ├── image_transform.h      # 画像変換ヘッダー
│   ├── image_transform.cpp    # 画像変換実装（C++コア）
│   └── bindings.cpp           # Emscriptenバインディング
├── web/
│   ├── index.html             # メインHTML
│   ├── style.css              # スタイルシート
│   ├── app.js                 # JavaScript制御
│   ├── image_transform.js     # 生成されたWebAssemblyラッパー
│   └── image_transform.wasm   # 生成されたWebAssemblyバイナリ
├── build.sh                   # ビルドスクリプト
└── README.md                  # このファイル
```

## 🔬 技術詳細

### C++コア（組込み環境への移植可能）

`src/image_transform.cpp`に実装された画像処理コアは、標準C++のみを使用しており、以下の機能を提供します：

- **Image構造体**: RGBAピクセルデータを保持
- **AffineParams構造体**: アフィン変換パラメータ
- **ImageProcessor クラス**:
  - レイヤー管理
  - アフィン変換（バイリニア補間）
  - アルファブレンディング
  - 画像合成

### アフィン変換の実装

逆変換方式を採用し、出力ピクセルごとに入力画像の対応位置を計算：

1. キャンバス中心を基準点とする
2. 平行移動の逆変換
3. 回転の逆変換
4. スケールの逆変換
5. バイリニア補間でピクセル値を取得

### WebAssemblyバインディング

Emscriptenの`embind`を使用してC++クラスをJavaScriptから呼び出し可能にしています。

## 🚀 組込み環境への移植

C++コア（`image_transform.h`と`image_transform.cpp`）は、以下の特徴により組込み環境への移植が容易です：

- **依存関係なし**: 標準C++のみを使用（OpenCVなど不要）
- **シンプルなAPI**: `ImageProcessor`クラスのみ
- **メモリ管理**: `std::vector`を使用（カスタムアロケータに変更可能）

### 移植手順

1. `src/image_transform.h`と`src/image_transform.cpp`をコピー
2. 必要に応じて`std::vector`をカスタムアロケータに置き換え
3. プロジェクトに組み込んでビルド

## 🛠️ 開発

### ビルドオプションのカスタマイズ

`build.sh`内のEmscriptenコンパイルオプション：

- `-O3`: 最適化レベル（速度優先）
- `-s ALLOW_MEMORY_GROWTH=1`: 動的メモリ確保
- `-s TOTAL_MEMORY=256MB`: 初期メモリサイズ
- `-s MAXIMUM_MEMORY=512MB`: 最大メモリサイズ

### デバッグビルド

```bash
emcc src/image_transform.cpp src/bindings.cpp \
    -o web/image_transform.js \
    -std=c++11 \
    -O0 -g \
    -s ASSERTIONS=1 \
    # ... その他のオプション
```

## 📄 ライセンス

このプロジェクトは自由に使用・改変できます。

## 🤝 貢献

バグ報告や機能提案は歓迎します。

## 📞 サポート

問題が発生した場合：

1. ブラウザのコンソールでエラーメッセージを確認
2. Emscriptenが正しくインストールされているか確認
3. ブラウザがWebAssemblyに対応しているか確認

## 🎉 楽しんでください！

画像変換を試して、面白い合成画像を作成してみてください！
