# アフィン変換の入力要求範囲分割

**ステータス**: 構想段階

**関連**: [IDEA_UPSTREAM_NODE_TYPE_MASK.md](IDEA_UPSTREAM_NODE_TYPE_MASK.md) - 分割要否の判定に使用

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

## 実装方針

### TransformNode での分割処理

```cpp
RenderResult TransformNode::pullProcess(const RenderRequest& request) {
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

| 回転角度 | 従来の効率 | 分割後の効率 | 改善率 |
|----------|-----------|-------------|--------|
| 45度 | 約1% | 約50% | 50倍 |
| 30度 | 約10% | 約50% | 5倍 |
| 10度 | 約30% | 約50% | 1.7倍 |
| 0度/90度 | 約100% | 分割不要 | - |

## 懸念事項

- 分割によるオーバーヘッド（pullProcess 呼び出し回数増加）
- 上流にフィルタがある場合、各分割で独立処理される
- 境界での浮動小数点丸め誤差（固定小数点化で軽減可能）

## 実装ステップ

1. [ ] shouldSplit() 判定ロジック（上流ノードタイプマスク参照）
2. [ ] computeSplits() 分割計算
3. [ ] TransformNode::pullProcess() での分割実行
4. [ ] mergeResults() 結果合成
5. [ ] パフォーマンス測定と効果検証

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/nodes/transform_node.h` | TransformNode 定義 |
| `src/fleximg/operations/transform.h` | アフィン変換実装 |
