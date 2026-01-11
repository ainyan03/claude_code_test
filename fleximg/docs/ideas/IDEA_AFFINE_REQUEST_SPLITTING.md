# AABB分割（アフィン変換の入力要求範囲最適化）

**ステータス**: 実装済み（効果検証中）

**関連**: [IDEA_UPSTREAM_NODE_TYPE_MASK.md](IDEA_UPSTREAM_NODE_TYPE_MASK.md) - 分割要否の判定に使用

## 用語定義

| 用語 | 英語 | 説明 |
|------|------|------|
| **AABB分割** | AABB Splitting | 本ドキュメントで提案する最適化手法 |
| タイル分割 | Tile Splitting | Rendererノードによる出力キャンバスの分割（別概念） |

**命名理由**: 「分割」は汎用的な語なので、アフィン変換特有の概念「AABB」と組み合わせることで、Rendererのタイル分割との混同を防ぐ。また、将来的にX/Y両方向の分割に拡張する可能性を考慮し、「帯状」「ストリップ」等の一方向を示す語は避けた。

## 概要

アフィン変換ノードが上流に要求するAABB（Axis-Aligned Bounding Box）を細分化することで、不要ピクセルの取得を削減し、パイプライン処理の効率を向上させる。

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

## 解決策：AABB分割

入力AABBを細分化し、各部分で本当に必要な範囲のみを要求する。初期実装ではY方向に分割する。

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

## AABB分割の判定基準

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

## 実装方針

### AffineNode でのAABB分割処理

```cpp
RenderResult AffineNode::pullProcess(const RenderRequest& request) {
    // 1. 分割要否を判定（上流ノードタイプマスクを参照）
    if (!shouldSplit()) {
        return processWithoutSplit(request);
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

    // 4. 各領域を分割して処理
    std::vector<RenderResult> results;
    // ... 領域1, 2, 3 を処理 ...

    // 5. 結果を合成して返す
    return mergeResults(results);
}
```

## 期待効果

| 回転角度 | 従来の効率 | AABB分割後の効率 | 改善率 |
|----------|-----------|------------------|--------|
| 45度 | 約1% | 約50% | 50倍 |
| 30度 | 約10% | 約50% | 5倍 |
| 10度 | 約30% | 約50% | 1.7倍 |
| 0度/90度 | 約100% | 分割不要 | - |

**現在のデバッグ表示**: Affineノードの `(aabb:Xx)` で改善倍率を確認可能。値が1.0xを超える場合、AABB分割の効果が期待できる。

## 懸念事項

- 分割によるオーバーヘッド（pullProcess 呼び出し回数増加）
- 上流にフィルタがある場合、各分割で独立処理される
- 境界での浮動小数点丸め誤差（固定小数点化で軽減可能）

## 実装ステップ

1. [x] AABB分割効果の見積もり機能（デバッグ表示に `aabb:Xx` として実装済み）
2. [x] shouldSplitAABB() 判定ロジック（閾値10倍以上で発動）
3. [x] pullProcessWithAABBSplit() 分割処理（動的分割数、X/Y両方向対応）
4. [x] 部分変換（既存applyAffine再利用、calcValidRangeで自動範囲制限）
5. [x] 動的分割戦略（縦横比で分割方向を決定、最小サイズ32px）
6. [x] 台形フィット（各stripのX/Y範囲を平行四辺形にフィット）
7. [ ] パフォーマンス測定と効果検証（継続）

## 現在の実装

```cpp
// AffineNode (affine_node.h)
static constexpr int MIN_SPLIT_SIZE = 32;         // 分割後の最小ピクセル数
static constexpr int MAX_SPLIT_COUNT = 8;         // 分割数の上限
static constexpr float AABB_SPLIT_THRESHOLD = 10.0f;  // 閾値（改善倍率10倍以上で発動）

struct SplitStrategy {
    bool splitInX;      // true: X方向分割, false: Y方向分割
    int splitCount;     // 分割数
};
```

### 分割戦略

- **分割方向**: AABBの縦横比で決定（長い辺を分割）
- **分割数**: 分割後のサイズが32ピクセル以上になるよう動的に決定（最大8分割）

## 台形フィット

各stripのX/Y範囲を平行四辺形の実際の幅にフィットさせることで、`requestedPixels` を削減。

### 実装

```cpp
// Y方向分割時：このY範囲で必要なX範囲を計算
std::pair<int, int> computeXRangeForYStrip(int yMin, int yMax, const InputRegion& region)

// X方向分割時：このX範囲で必要なY範囲を計算
std::pair<int, int> computeYRangeForXStrip(int xMin, int xMax, const InputRegion& region)
```

### 処理フロー

```
分割前:                    台形フィット後:
┌─────────────┐           ┌───┐
│   strip 0   │          ┌┴───┴┐     ← 必要範囲のみ要求
├─────────────┤    →    ┌┴─────┴┐
│   strip 1   │        ┌┴───────┴┐
├─────────────┤       ┌┴─────────┴┐
│   strip 2   │       └───────────┘
└─────────────┘
```

## 残課題

1. **上流ノードタイプマスク**: 分割要否の判定に活用
2. **三角領域の最適化**: 領域1/3の50%効率を改善可能か検討

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/nodes/affine_node.h` | AffineNode（AABB分割実装） |
| `src/fleximg/common.h` | toFixed16()（順変換行列） |
