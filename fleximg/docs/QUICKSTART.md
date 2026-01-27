# クイックスタートガイド

## すぐに試したい方へ

### 最も簡単な方法: GitHub Pagesでアクセス

**https://ainyan03.github.io/claude_code_test/**

ビルド不要でブラウザから直接利用できます。WebAssembly版が動作します。

### ローカルで動かす場合

#### 1. Webサーバーを起動

```bash
cd fleximg/demo/web
python3 server.py
```

#### 2. ブラウザでアクセス

PCから: **http://localhost:8080**

スマホから: **http://[PCのIPアドレス]:8080**
- 例: `http://192.168.1.100:8080`

> **Note**: ビルド済みのWebAssemblyファイルが`demo/web/`ディレクトリに含まれているため、すぐに動作します。

## スマートフォンからアクセスする方法

### PCのIPアドレスを確認

**Linux/Mac:**
```bash
ip addr show | grep "inet " | grep -v 127.0.0.1
# または
ifconfig | grep "inet " | grep -v 127.0.0.1
```

**Windows:**
```cmd
ipconfig
```

### スマホのブラウザで開く

1. PCと同じWi-Fiネットワークに接続
2. ブラウザで `http://[PCのIPアドレス]:8080` を開く
3. 画像をアップロードして試す

## 基本的な使い方

1. **画像を追加**: 画像ライブラリの「+ 画像追加」ボタンをクリック
2. **ノードを追加**: グラフエリアを右クリックしてメニューから選択
   - 画像ノード: ライブラリから画像を選択
   - アフィン変換ノード: 画像を変換
   - 合成ノード: 複数画像を合成
   - フィルタノード: 明るさ・グレースケール・ぼかし・アルファ
   - 出力ノード: 結果をプレビュー
3. **ノードを接続**: 出力ポート（右側）から入力ポート（左側）へドラッグ
4. **パラメータ調整**: スライダーを動かして変換を適用
   - X位置/Y位置: 画像を移動
   - 回転: 0〜360度回転
   - スケール: 拡大縮小
   - 透過度: 透明度を調整
5. **保存**: 「合成画像をダウンロード」で保存

## 自分でビルドしたい方へ

最新のコードをビルドする場合は、以下の手順に従ってください。

### 必要なもの

- Emscripten SDK

### ビルド手順

```bash
# Emscriptenのインストール
git clone https://github.com/emscripten-core/emsdk.git
cd emsdk
./emsdk install latest
./emsdk activate latest
source ./emsdk_env.sh

# プロジェクトディレクトリに戻る
cd path/to/fleximg

# ビルド（デバッグモード推奨：性能計測・デバッグ情報が有効）
./build.sh --debug

# リリースビルド（性能計測無効、サイズ最適化）
# ./build.sh

# サーバー起動
cd demo/web
python3 server.py
```

ビルドが成功すると、`demo/web/`ディレクトリに `fleximg.wasm` と `fleximg.js` が生成されます。

## トラブルシューティング

### 画像が表示されない

- ブラウザのコンソール（F12）でエラーを確認
- ブラウザをリロード（Ctrl+R / Cmd+R）
- WebAssemblyファイル（fleximg.wasm）が正しく読み込まれているか確認

### スマホからアクセスできない

- PCとスマホが同じWi-Fiに接続されているか確認
- PCのファイアウォール設定を確認
- ポート8080が他のアプリで使われていないか確認

### 動作が遅い

- 画像サイズを小さくする
- ノード数を減らす
- ブラウザのハードウェアアクセラレーションを有効にする

## 次のステップ

詳細なドキュメントは以下をご覧ください:

- [README.md](../README.md) - 概要
- [ARCHITECTURE.md](ARCHITECTURE.md) - アーキテクチャ詳細
