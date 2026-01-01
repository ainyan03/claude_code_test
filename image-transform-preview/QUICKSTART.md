# 🚀 クイックスタートガイド

## すぐに試したい方へ（ビルド不要）

WebAssemblyのビルドなしで、すぐに動作確認できます！

### 1. Webサーバーを起動

```bash
cd image-transform-preview/web
python3 -m http.server 8080
```

### 2. ブラウザでアクセス

PCから: **http://localhost:8080**

スマホから: **http://[PCのIPアドレス]:8080**
- 例: `http://192.168.1.100:8080`

> **Note**: JavaScript版が自動的に使用されます。WebAssemblyをビルドすると、より高速に動作します。

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

1. **画像を追加**: 「+ 画像追加」ボタンをタップ
2. **パラメータ調整**: スライダーを動かして変換を適用
   - X位置/Y位置: 画像を移動
   - 回転: 0〜360度回転
   - スケール: 拡大縮小
   - 透過度: 透明度を調整
3. **レイヤー管理**: ↑/↓で順序変更、×で削除
4. **保存**: 「📥 合成画像をダウンロード」で保存

## WebAssembly版を試したい方へ

より高速な処理を体験したい場合は、WebAssemblyをビルドしてください。

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
cd path/to/image-transform-preview

# ビルド
./build.sh

# サーバー起動
cd web
python3 -m http.server 8080
```

ビルド後、ブラウザのコンソールに "Using WebAssembly backend" と表示されます。

## トラブルシューティング

### 画像が表示されない

- ブラウザのコンソール（F12）でエラーを確認
- ブラウザをリロード（Ctrl+R / Cmd+R）

### スマホからアクセスできない

- PCとスマホが同じWi-Fiに接続されているか確認
- PCのファイアウォール設定を確認
- ポート8080が他のアプリで使われていないか確認

### 動作が遅い

- WebAssembly版をビルドすると高速化します
- 画像サイズを小さくする
- レイヤー数を減らす

## 次のステップ

詳細なドキュメントは [README.md](README.md) をご覧ください。

楽しんでください！🎉
