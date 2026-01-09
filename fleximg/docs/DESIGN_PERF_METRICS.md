# パフォーマンス測定基盤設計

## 概要

ノードタイプ別にパフォーマンスメトリクスを収集し、最適化の効果を定量評価するための基盤。

## 目的

- 各ノードタイプの処理時間を計測
- メモリ確保のコストを可視化
- ピクセル効率（要求 vs 使用）を評価
- 最適化の前後比較を可能にする

## 設計

### ノードタイプ

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

### メトリクス構造体

```cpp
struct NodeMetrics {
    uint32_t time_us = 0;         // 処理時間（マイクロ秒）
    int count = 0;                // 呼び出し回数
    size_t allocBytes = 0;        // メモリ確保バイト数
    int allocCount = 0;           // メモリ確保回数
    uint64_t requestedPixels = 0; // 上流に要求したピクセル数
    uint64_t usedPixels = 0;      // 実際に使用したピクセル数

    void reset() {
        *this = NodeMetrics{};
    }

    // 不要ピクセル率（0.0〜1.0）
    float wasteRatio() const {
        if (requestedPixels == 0) return 0;
        return 1.0f - (float)usedPixels / requestedPixels;
    }
};

struct PerfMetrics {
    NodeMetrics nodes[NodeType::Count];

    void reset() {
        for (auto& n : nodes) n.reset();
    }

    // 全ノード合計のメモリ確保バイト数
    size_t totalAllocBytes() const {
        size_t sum = 0;
        for (const auto& n : nodes) sum += n.allocBytes;
        return sum;
    }

    // 全ノード合計の処理時間
    uint32_t totalTime() const {
        uint32_t sum = 0;
        for (const auto& n : nodes) sum += n.time_us;
        return sum;
    }
};
```

## 計測ポイント

### 処理時間

各ノードの `evaluate()` 内で計測：

```cpp
EvalResult FilterEvalNode::evaluate(const RenderRequest& request,
                                    const RenderContext& context) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto start = std::chrono::high_resolution_clock::now();
#endif

    // ... 処理 ...

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    auto end = std::chrono::high_resolution_clock::now();
    if (context.perfMetrics) {
        auto& m = context.perfMetrics->nodes[NodeType::Filter];
        m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
        m.count++;
    }
#endif
    return result;
}
```

### メモリ確保

`ImageBuffer` コンストラクタで計測：

```cpp
ImageBuffer::ImageBuffer(int w, int h, PixelFormatID fmtID, ImageAllocator* alloc)
    : ViewPort(...) {

    allocateMemory();

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    // グローバルまたはTLSでメトリクスを参照
    if (auto* metrics = getCurrentPerfMetrics()) {
        // 呼び出し元のノードタイプを特定する必要あり
        // → RenderContext経由で渡す or TLSで管理
    }
#endif
}
```

**課題**: ImageBuffer確保時に「どのノードタイプから呼ばれたか」を知る方法が必要。

**解決案**:
1. TLS（Thread Local Storage）で現在のノードタイプを管理
2. RenderContextにcurrentNodeTypeを追加
3. ImageBuffer確保はノード別ではなく全体集計

### ピクセル効率

上流への要求時と結果使用時に計測：

```cpp
EvalResult AffineEvalNode::evaluate(const RenderRequest& request,
                                    const RenderContext& context) {
    RenderRequest inputReq = computeInputRequest(request);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    if (context.perfMetrics) {
        auto& m = context.perfMetrics->nodes[NodeType::Affine];
        m.requestedPixels += inputReq.width * inputReq.height;
    }
#endif

    EvalResult inputResult = inputs[0]->evaluate(inputReq, context);

    // ... アフィン変換処理 ...

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    if (context.perfMetrics) {
        auto& m = context.perfMetrics->nodes[NodeType::Affine];
        // 実際に使用したピクセル数（出力サイズ）
        m.usedPixels += request.width * request.height;
    }
#endif

    return result;
}
```

## WASM/JS連携

### C++側エクスポート

```cpp
// bindings.cpp
PerfMetrics* NodeGraphEvaluator::getPerfMetricsPtr() {
    return &perfMetrics;
}

// Embind
EMSCRIPTEN_BINDINGS(perf_metrics) {
    class_<NodeMetrics>("NodeMetrics")
        .property("time_us", &NodeMetrics::time_us)
        .property("count", &NodeMetrics::count)
        .property("allocBytes", &NodeMetrics::allocBytes)
        .property("allocCount", &NodeMetrics::allocCount)
        .property("requestedPixels", &NodeMetrics::requestedPixels)
        .property("usedPixels", &NodeMetrics::usedPixels)
        .function("wasteRatio", &NodeMetrics::wasteRatio);
}
```

### JS側での表示

```javascript
function displayPerfMetrics(evaluator) {
    const metrics = evaluator.getPerfMetrics();
    const nodeNames = ['Image', 'Filter', 'Affine', 'Composite', 'Output'];

    console.log('=== Performance Metrics ===');
    for (let i = 0; i < 5; i++) {
        const m = metrics.nodes[i];
        if (m.count > 0) {
            console.log(`${nodeNames[i]}: ${m.count} calls, ${m.time_us}us`);
            if (m.requestedPixels > 0) {
                console.log(`  Pixels: ${m.usedPixels}/${m.requestedPixels} (waste: ${(m.wasteRatio() * 100).toFixed(1)}%)`);
            }
        }
    }
}
```

## 既存コードとの互換性

### 移行方針

既存の `PerfMetricIndex` ベースのコードを段階的に移行：

```cpp
// 既存（廃止予定）
context.perfMetrics->add(PerfMetricIndex::Filter, elapsed);

// 新方式
context.perfMetrics->nodes[NodeType::Filter].time_us += elapsed;
context.perfMetrics->nodes[NodeType::Filter].count++;
```

### 後方互換ヘルパー（オプション）

```cpp
// 移行期間中の互換レイヤー
namespace PerfMetricIndex {
    constexpr int Filter = NodeType::Filter;
    constexpr int Affine = NodeType::Affine;
    // ...
}

// 既存のadd()をラップ
void PerfMetrics::add(int nodeType, uint32_t us) {
    nodes[nodeType].time_us += us;
    nodes[nodeType].count++;
}
```

## 実装ステップ

1. [ ] `NodeType` 名前空間を `node_graph.h` に追加
2. [ ] `NodeMetrics` 構造体を定義
3. [ ] `PerfMetrics` を新構造に置き換え
4. [ ] 各ノードの `evaluate()` で新APIを使用
5. [ ] WASM bindingsを更新
6. [ ] JS側の表示を更新
7. [ ] メモリ確保の計測方法を決定・実装

## 関連ファイル

| ファイル | 変更内容 |
|---------|---------|
| `src/fleximg/node_graph.h` | NodeType, NodeMetrics, PerfMetrics定義 |
| `src/fleximg/evaluation_node.cpp` | 各ノードの計測コード |
| `demo/bindings.cpp` | WASM bindingsの更新 |
| `demo/web/app.js` | 表示UIの更新 |
