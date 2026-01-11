# 案D: アルファマスク併用

## 概要

非アルファフォーマットの画像に対して、別途アルファマスクを管理する。
画像データとアルファ情報を分離することで、元のフォーマットを維持しつつ
透明度を表現する。

## 設計

### データ構造

```cpp
struct RenderResult {
    ImageBuffer buffer;
    Point origin;

    // 新規: アルファマスク（オプション）
    // - 非アルファフォーマットの透明度を表現
    // - nullopt = 全ピクセル不透明
    // - 1bit/pixel または 8bit/pixel
    std::optional<ImageBuffer> alphaMask;
};
```

### アルファマスクの形式

**1bit マスク（メモリ効率重視）**
```
- 1 = 有効（不透明）、0 = 無効（透明）
- メモリ: (width * height) / 8 バイト
- 半透明は表現不可
```

**8bit マスク（精度重視）**
```
- 0-255 の透明度
- メモリ: width * height バイト
- 半透明も表現可能
```

### AffineNode の変更

```cpp
RenderResult process(RenderResult&& input, const RenderRequest& request) override {
    bool needsMask = !PixelFormatRegistry::getInstance()
                      .getFormat(input.buffer.formatID())->hasAlpha;

    ImageBuffer output(request.width, request.height, input.buffer.formatID());

    std::optional<ImageBuffer> mask;
    if (needsMask) {
        // 1bit マスクを作成（全て0 = 透明で初期化）
        mask = ImageBuffer(request.width, request.height,
                          PixelFormatIDs::ALPHA1);  // 仮のフォーマットID
    }

    // アフィン変換（マスクも同時に更新）
    applyAffineWithMask(output.view(), mask ? &mask->view() : nullptr, ...);

    return RenderResult(std::move(output), request.origin, std::move(mask));
}
```

### CompositeNode の変更

```cpp
void blendWithMask(ViewPort& dst, const RenderResult& src) {
    if (src.alphaMask) {
        // マスクを参照してブレンド
        for (int y = 0; y < src.buffer.height(); y++) {
            for (int x = 0; x < src.buffer.width(); x++) {
                if (src.alphaMask->getAlphaAt(x, y) > 0) {
                    // 有効ピクセルのみブレンド
                    blendPixel(dst, src, x, y);
                }
            }
        }
    } else {
        // 従来通り
        blend::onto(...);
    }
}
```

## メリット

- **フォーマット維持**: RGB565 等のまま処理可能
- **精密**: ピクセル単位の透明度制御
- **拡張性**: 半透明効果も可能（8bit マスク時）

## デメリット

- **メモリ増加**: マスク分のメモリが追加
  - 1bit: +12.5%
  - 8bit: +50%（RGB565比）
- **複雑度**: 全ノードでマスク対応が必要
- **性能**: ピクセル単位のマスクチェック

## 考慮事項

### マスク伝播

```
[Source] → [Affine] → [Filter] → [Composite]
              ↓           ↓
           マスク生成   マスク維持?
```

フィルタノード等でマスクをどう扱うか検討が必要。

### 既存フォーマットとの関係

アルファ付きフォーマット（RGBA8）では alphaMask は不要。
混在時の処理ルールを定義する必要がある。

### 1bit vs 8bit

| 項目 | 1bit | 8bit |
|------|------|------|
| メモリ | 小 | 中 |
| 精度 | 2値 | 256段階 |
| 用途 | 有効/無効のみ | 半透明対応 |

アフィン変換の境界では半透明が望ましい場合もある（アンチエイリアス）。

## 実装コスト

高。新しいデータ構造と全ノードの対応が必要。

## 関連

- レイヤーマスク（画像編集ソフトの概念）
- ステンシルバッファ（3Dグラフィックス）
