# アフィン変換の入力要求範囲分割

> **状態**: 未実装（設計検討段階）
>
> **前提**: パフォーマンス計測基盤は整備済み。効率はブラウザコンソールで確認可能。

## 概要

アフィン変換ノードが上流に要求する範囲を分割することで、不要ピクセルの取得を削減し、パイプライン処理の効率を向上させる。

## 問題

45度回転等の場合、出力タイル（例: 256×1のスキャンライン）を逆変換したAABBが巨大化し、無駄な領域を上流に要求してしまう。

```
出力タイル: 256×1
    ↓ 45度逆変換
必要領域: 斜めの細い帯
    ↓ AABB化
要求範囲: 約181×181 ≈ 32,761ピクセル

実際に必要なのは約362ピクセル（効率約1%）
```

## 解決策

入力AABBをY方向に分割し、各Y範囲で本当に必要なX範囲のみを要求する。

```
入力空間での必要領域（斜めの帯）:

Y=0-60:   ■■□□□  → X=0-60 のみ要求
Y=61-120: □■■□□  → X=60-120 のみ要求
Y=121-180:□□■■■  → X=120-180 のみ要求

各要求: 約60×60 = 3,600
合計: 約10,800ピクセル（元の約33%）
```

## 平行四辺形の3領域構造

出力タイルを逆変換すると平行四辺形になる。4頂点をY座標でソートすると、3つの領域に分かれる。

```
4頂点: p0(Y最小), p1, p3, p2(Y最大)

          p0 (top)
         / \
        /   \
       /     \        ← 領域1: 三角形（幅が線形増加）
      /       p1
     /       /
    p3      /         ← 領域2: 平行四辺形（幅一定）
     \     /
      \   /           ← 領域3: 逆三角形（幅が線形減少）
       \ /
        p2 (bottom)
```

### 各領域の特性

| 領域 | Y範囲 | 幅の変化 | 効率（頂点から分割時）|
|------|-------|----------|----------------------|
| 1 | p0.y → min(p1.y, p3.y) | 0 → w（線形増加）| 常に50% |
| 2 | min(p1.y, p3.y) → max(p1.y, p3.y) | w（一定）| 計算で最適化可能 |
| 3 | max(p1.y, p3.y) → p2.y | w → 0（線形減少）| 常に50% |

**注**: 45度回転の場合、p1.y == p3.y となり、領域2は消滅する。

## 分割基準

### 2つの基準を組み合わせる

1. **比率基準**: 必要ピクセル / 総ピクセル ≥ threshold（例: 50%）
2. **絶対数基準**: 不要ピクセル ≤ maxWastePixels

```cpp
bool needsSplit(int required, int total, float ratioThreshold, int maxWaste) {
    int waste = total - required;
    float ratio = (float)required / total;

    return ratio < ratioThreshold || waste > maxWaste;
}
```

### 分割高さの閉形式解（領域2）

平行四辺形領域での最適分割高さ:

```
比率基準から:
  h ≤ w × (1 - threshold) / (threshold × |slope|)

絶対数基準から:
  waste = slope × h²
  h ≤ sqrt(maxWaste / slope)

最終的な分割高さ = min(h1, h2)
```

## 上流ノードタイプの判定

### ビットマスク方式

パイプライン構築時に上流ノードの構成をビットマスクで伝播させる。

```cpp
enum NodeTypeMask : uint32_t {
    NONE         = 0,
    IMAGE        = 1 << 0,  // 元画像ノード
    LIGHT_FILTER = 1 << 1,  // Brightness, Grayscale, Alpha
    AFFINE       = 1 << 2,  // アフィン変換
    HEAVY_FILTER = 1 << 3,  // Blur等
    COMPOSITE    = 1 << 4,  // 合成
};
```

### 伝播の仕組み

```cpp
class EvaluationNode {
protected:
    uint32_t upstreamMask_ = 0;

public:
    virtual uint32_t getOwnMask() const = 0;

    void propagateMask() {
        for (auto* input : inputs) {
            upstreamMask_ |= input->upstreamMask_
                          |  input->getOwnMask();
        }
    }
};
```

### 分割判定

```cpp
bool AffineEvalNode::shouldSplit() const {
    // 上流が ImageNode のみなら分割不要
    if (upstreamMask_ == 0) return false;

    // 上流に重いフィルタがあれば分割推奨
    return (upstreamMask_ & NodeTypeMask::HEAVY_FILTER) != 0;
}
```

## 実装アルゴリズム

```cpp
std::vector<Split> AffineEvalNode::computeSplits(
    const RenderRequest& request,
    float ratioThreshold,
    int maxWastePixels) const {

    // 1. 上流が軽い場合は分割不要
    if (!shouldSplit()) {
        return {};  // 分割なし
    }

    // 2. 4頂点を計算してY座標でソート
    auto vertices = computeInverseTransformedVertices(request);
    std::sort(vertices.begin(), vertices.end(),
              [](auto& a, auto& b) { return a.y < b.y; });

    // 3. 3領域を特定
    float y0 = vertices[0].y;
    float y1 = std::min(vertices[1].y, vertices[3].y);
    float y2 = std::max(vertices[1].y, vertices[3].y);
    float y3 = vertices[2].y;

    std::vector<Split> splits;

    // 4. 領域1（三角形）
    if (ratioThreshold <= 0.5f) {
        splits.push_back({y0, y1});
    } else {
        splits.append(splitTriangle(y0, y1, ratioThreshold, maxWastePixels));
    }

    // 5. 領域2（平行四辺形）- 存在する場合
    if (y2 - y1 > 1) {
        float w = parallelogramWidth;
        float slope = std::abs(invB);

        // 比率基準
        float h1 = w * (1 - ratioThreshold) / (ratioThreshold * slope);
        // 絶対数基準
        float h2 = std::sqrt(maxWastePixels / slope);

        float maxH = std::min(h1, h2);
        float region2Height = y2 - y1;
        int numSplits = std::max(1, (int)std::ceil(region2Height / maxH));
        float splitH = region2Height / numSplits;

        for (int i = 0; i < numSplits; i++) {
            splits.push_back({y1 + i * splitH, y1 + (i + 1) * splitH});
        }
    }

    // 6. 領域3（逆三角形）
    if (ratioThreshold <= 0.5f) {
        splits.push_back({y2, y3});
    } else {
        splits.append(splitTriangle(y2, y3, ratioThreshold, maxWastePixels));
    }

    return splits;
}
```

## 期待効果

| 回転角度 | 従来の効率 | 分割後の効率 | 改善率 |
|----------|-----------|-------------|--------|
| 45度 | 約1% | 約50% | 50倍 |
| 30度 | 約10% | 約50% | 5倍 |
| 10度 | 約30% | 約50% | 1.7倍 |
| 0度/90度 | 約100% | 分割不要 | - |

## 懸念事項

- 分割によるオーバーヘッド（evaluate呼び出し回数増加）
- 上流にフィルタがある場合、各分割で独立処理される
- 境界での浮動小数点丸め誤差

## 実装ステップ

1. [ ] NodeTypeMaskとビットマスク伝播の実装
2. [ ] shouldSplit() 判定ロジック
3. [ ] computeSplits() 分割計算
4. [ ] AffineEvalNode::evaluate() での分割実行
5. [ ] パフォーマンス測定と効果検証

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/evaluation_node.h` | EvaluationNode基底クラス |
| `src/fleximg/evaluation_node.cpp` | AffineEvalNode実装 |
