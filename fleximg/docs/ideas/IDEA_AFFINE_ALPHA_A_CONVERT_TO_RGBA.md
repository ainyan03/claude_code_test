# 案A: アフィン前にRGBA8に変換

## 概要

AffineNode で非アルファフォーマットを検出し、処理前に RGBA8_Straight に変換する。
アフィン変換後、必要に応じて元のフォーマットに戻す。

## 設計

### 変換フロー

```
[RGB565入力] → [RGBA8変換] → [アフィン変換] → [RGBA8出力]
                  ↓
           alpha = 255 で初期化
                  ↓
           範囲外は alpha = 0
```

### 実装箇所

**AffineNode::process() / pullProcess()**

```cpp
RenderResult process(RenderResult&& input, const RenderRequest& request) override {
    PixelFormatID inputFmt = input.buffer.formatID();
    bool needsAlpha = !PixelFormatRegistry::getInstance()
                       .getFormat(inputFmt)->hasAlpha;

    if (needsAlpha) {
        // RGBA8 に変換（alpha = 255）
        input = RenderResult(
            std::move(input.buffer).toFormat(PixelFormatIDs::RGBA8_Straight),
            input.origin
        );
    }

    // 以降は従来通りの処理
    // 出力バッファは RGBA8 でゼロ初期化（alpha = 0 = 透明）
    ...
}
```

### 出力フォーマット

- 非アルファ入力 → RGBA8 出力（アルファ情報を保持）
- アルファ入力 → 入力と同じフォーマット出力

## メリット

- **シンプル**: 既存の `toFormat()` を使用、追加APIなし
- **確実**: 範囲外が確実に透明になる
- **互換性**: 下流ノードは変更不要

## デメリット

- **メモリ増加**: RGB565(2B) → RGBA8(4B) で2倍
- **変換コスト**: 入力時に全ピクセル変換
- **フォーマット変化**: 出力が入力と異なるフォーマットになる

## 考慮事項

### 出力フォーマットの選択

1. **常にRGBA8**: シンプルだが、組み込み向けフォーマットの意味が薄れる
2. **元フォーマットに戻す**: 再変換コスト、アルファ情報の損失
3. **ユーザー選択**: 設定で制御可能に

### CompositeNode との連携

先の修正で CompositeNode は非対応フォーマットを RGBA16 に変換するため、
AffineNode の出力が RGBA8 なら問題なく合成される。

## 実装コスト

低〜中。既存の変換インフラを活用可能。

## 関連

- `ImageBuffer::toFormat()`
- `PixelFormatDescriptor::hasAlpha`
