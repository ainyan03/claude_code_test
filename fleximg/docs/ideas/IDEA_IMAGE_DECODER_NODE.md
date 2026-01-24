# 画像デコーダーノード（RendererNode派生）

**ステータス**: 構想段階

## 概要

JPEG/PNG等の画像デコードを RendererNode 派生型として実装し、フォーマット固有のブロック単位でタイル処理を行う。

## 設計方針

- RendererNode は「タイル分割戦略の決定者」であり、デコーダー派生型がフォーマット固有の制約を課す
- 画像フォーマットの特性上、ブロック順序（ラスタースキャン等）の強制は妥当な制約

## 実装内容（JpegDecoderNode 例）

### クラス構造

```cpp
class JpegDecoderNode : public RendererNode {
public:
    void setSource(const uint8_t* jpegData, size_t size);
protected:
    void execPrepare() override;   // ヘッダ解析、MCUサイズ設定
    void execProcess() override;   // MCU順ループ
    void processTile(int tileX, int tileY) override;
};
```

### execPrepare()

- JPEG ヘッダ解析、画像サイズ・MCUサイズ取得
- `setVirtualScreen(imageWidth, imageHeight)`
- `setTileConfig(mcuWidth, mcuHeight)`

### execProcess()

- MCU 行単位でデコード（libjpeg-turbo のスキャンライン API 活用）
- MCU 順（ラスタースキャン）で processTile() を呼び出し

### processTile()

- デコード済み MCU から RenderResponse 生成
- 下流ノードへ push

## 利点

- **メモリ効率**: 全画像を一度にデコードせず、必要な部分だけ処理
- **パイプライン再利用**: 既存のフィルター/シンクノードをそのまま活用
- **制約の明示性**: クラス選択で MCU 順処理が明示される

## 検討事項

- MCU 行単位のバッファ管理（1行分をキャッシュし、processTile で切り出す）
- 入力ポート数: 発火点兼データソースなので0ポート化も検討
- 将来拡張: PNG（スキャンライン単位）、WebP（ブロック単位）等のフォーマット対応
