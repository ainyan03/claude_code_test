# 案C: アフィン出力をAABB範囲に限定

## 概要

アフィン変換の出力バッファを、変換後の画像が実際に存在する AABB 範囲に限定する。
これにより、出力バッファに「範囲外」が存在しなくなる。

## 設計

### 現状 vs 提案

**現状（request サイズで出力）**
```
Request: 640x480, origin(320,240)
    ┌─────────────────────────────┐
    │                             │
    │         ◇                   │  ← 回転画像
    │        ◇ ◇                  │
    │         ◇                   │
    │                             │  ← 範囲外 = ゼロ = 不透明黒
    │                             │
    └─────────────────────────────┘
出力: 640x480 バッファ（大部分がゼロ）
```

**提案（AABB サイズで出力）**
```
    ┌───────┐
    │  ◇    │  ← 回転画像の AABB
    │ ◇ ◇   │
    │  ◇    │
    └───────┘
出力: 小さいバッファ + origin 調整
```

### 実装

**AffineNode::process()**

```cpp
RenderResult process(RenderResult&& input, const RenderRequest& request) override {
    // 入力画像の4隅を順変換して出力 AABB を計算
    auto [minX, minY, maxX, maxY] = computeOutputAABB(input);

    // request 範囲との交差を計算
    int outLeft = std::max(minX, -from_fixed8(request.origin.x));
    int outTop = std::max(minY, -from_fixed8(request.origin.y));
    int outRight = std::min(maxX, request.width - from_fixed8(request.origin.x));
    int outBottom = std::min(maxY, request.height - from_fixed8(request.origin.y));

    int outW = outRight - outLeft;
    int outH = outBottom - outTop;

    if (outW <= 0 || outH <= 0) {
        return RenderResult();  // 完全に範囲外
    }

    // AABB サイズで出力バッファを作成
    ImageBuffer output(outW, outH, input.buffer.formatID());

    // origin を調整
    Point newOrigin = {
        to_fixed8(-outLeft),
        to_fixed8(-outTop)
    };

    // アフィン変換を適用
    applyAffine(output.view(), newOrigin.x, newOrigin.y, ...);

    return RenderResult(std::move(output), newOrigin);
}
```

### CompositeNode との連携

CompositeNode は origin を使って配置するため、
AABB サイズの出力でも正しい位置に合成される。

## メリット

- **メモリ効率**: 無駄なゼロ領域を確保しない
- **変換不要**: 入力フォーマットを維持
- **シンプル**: 既存の座標系を活用

## デメリット

- **AABB の角**: 回転画像の AABB 内でも、角は範囲外（ゼロ）のまま
- **完全解決ではない**: 以下の図のように角が残る

```
AABB で切り出しても角は残る:
    ┌───────┐
    │▓ ◇  ▓│  ← ▓ = 範囲外（ゼロ）
    │ ◇ ◇  │
    │▓ ◇  ▓│
    └───────┘
```

## 考慮事項

### タイル分割との組み合わせ

この案は「タイル分割と同じアプローチを非分割時にも適用する」と考えられる。

タイル分割時：各タイルが AABB サイズで出力
非分割時：全体を1つの AABB サイズで出力

### 角の問題の軽減

1. **許容**: 角の黒は小さいので許容
2. **案Aと併用**: AABB出力 + RGBA変換で角も透明に
3. **案Bと併用**: AABB出力 + 有効範囲情報で角をマスク

## 実装コスト

低〜中。pushProcess には類似ロジックが既に存在。

## 関連

- `AffineNode::pushProcess()` - 順変換で AABB を計算する既存コード
- タイル分割処理
