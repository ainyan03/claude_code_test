# パフォーマンス計測基盤

## 概要

ノードタイプ別にパフォーマンスメトリクスを収集し、最適化の効果を定量評価するための基盤。

**有効化**: `./build.sh --debug` でビルド（`FLEXIMG_DEBUG_PERF_METRICS` マクロ）

## データ構造

### NodeType

```cpp
namespace NodeType {
    constexpr int Image = 0;
    constexpr int Filter = 1;
    constexpr int Affine = 2;
    constexpr int Composite = 3;
    constexpr int Output = 4;
    constexpr int Count = 5;
}
```

### NodeMetrics

```cpp
struct NodeMetrics {
    uint32_t time_us = 0;         // 処理時間（マイクロ秒）
    int count = 0;                // 呼び出し回数
    size_t allocBytes = 0;        // メモリ確保バイト数（未実装）
    int allocCount = 0;           // メモリ確保回数（未実装）
    uint64_t requestedPixels = 0; // 上流に要求したピクセル数
    uint64_t usedPixels = 0;      // 実際に使用したピクセル数

    float wasteRatio() const;     // 不要ピクセル率（0.0〜1.0）
};

struct PerfMetrics {
    NodeMetrics nodes[NodeType::Count];

    uint32_t totalTime() const;
    size_t totalAllocBytes() const;
};
```

## 計測ポイント

### 時間計測（全5ノード実装済み）

| ノード | 計測範囲 |
|--------|----------|
| Image | `evaluate()` 全体（画像データコピー含む） |
| Filter | オペレーター処理のみ（上流評価除外） |
| Affine | オペレーター処理のみ（上流評価除外） |
| Composite | 合成処理のみ（上流評価除外、フォーマット変換含む） |
| Output | 出力処理全体（上流評価含む） |

各ノードの `evaluate()` 内で処理時間を計測：

```cpp
#ifdef FLEXIMG_DEBUG_PERF_METRICS
auto start = std::chrono::high_resolution_clock::now();
#endif

// ... 処理 ...

#ifdef FLEXIMG_DEBUG_PERF_METRICS
if (context.perfMetrics) {
    auto& m = context.perfMetrics->nodes[NodeType::Filter];
    m.time_us += duration_cast<microseconds>(now() - start).count();
    m.count++;
}
#endif
```

### ピクセル効率計測

Filter/Affineノードで入力要求サイズと出力サイズを比較：

- `requestedPixels`: `computeInputRequest()` で計算した入力AABBサイズ
- `usedPixels`: 出力要求サイズ（`request.width * request.height`）
- `wasteRatio`: `1.0 - usedPixels / requestedPixels`

## JS側での取得

```javascript
const metrics = evaluator.getPerfMetrics();

// nodes配列でアクセス
for (let i = 0; i < metrics.nodes.length; i++) {
    const m = metrics.nodes[i];
    console.log(`time: ${m.time_us}us, count: ${m.count}`);
    if (m.requestedPixels > 0) {
        console.log(`efficiency: ${(1 - m.wasteRatio) * 100}%`);
    }
}

// 後方互換フラットキー
console.log(metrics.filterTime, metrics.affineTime);
```

## 未実装項目

### メモリ確保の計測

`allocBytes` / `allocCount` は現在未実装。課題：

- ImageBuffer確保時に「どのノードタイプから呼ばれたか」を知る方法が必要

解決案：
1. TLS（Thread Local Storage）で現在のノードタイプを管理
2. RenderContextにcurrentNodeTypeを追加
3. ノード別ではなく全体集計のみ

## 関連ファイル

| ファイル | 内容 |
|---------|------|
| `src/fleximg/node_graph.h` | NodeType, NodeMetrics, PerfMetrics 定義 |
| `src/fleximg/evaluation_node.cpp` | 各ノードの計測コード |
| `demo/bindings.cpp` | WASM bindings |
| `demo/web/app.js` | 表示UI |
