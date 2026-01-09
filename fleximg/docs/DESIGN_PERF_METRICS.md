# パフォーマンス計測基盤

## 概要

ノードタイプ別にパフォーマンスメトリクスを収集し、最適化の効果を定量評価するための基盤。

**有効化**: `./build.sh --debug` でビルド（`FLEXIMG_DEBUG_PERF_METRICS` マクロ）

## データ構造

### NodeType

```cpp
namespace NodeType {
    constexpr int Source = 0;
    constexpr int Filter = 1;
    constexpr int Transform = 2;
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
    uint64_t requestedPixels = 0; // 上流に要求したピクセル数
    uint64_t usedPixels = 0;      // 実際に使用したピクセル数
    uint64_t allocatedBytes = 0;  // このノードが確保したバイト数
    int allocCount = 0;           // 確保回数
    uint64_t maxAllocBytes = 0;   // 一回の最大確保バイト数
    int maxAllocWidth = 0;        // その時の幅
    int maxAllocHeight = 0;       // その時の高さ

    float wasteRatio() const;     // 不要ピクセル率（0.0〜1.0）
    void recordAlloc(size_t bytes, int width, int height);
};

struct PerfMetrics {
    NodeMetrics nodes[NodeType::Count];

    // グローバル統計
    uint64_t totalAllocatedBytes = 0;  // 累計確保バイト数
    uint64_t peakMemoryBytes = 0;      // ピークメモリ使用量
    uint64_t currentMemoryBytes = 0;   // 現在のメモリ使用量
    uint64_t maxAllocBytes = 0;        // 一回の最大確保バイト数
    int maxAllocWidth = 0;             // その時の幅
    int maxAllocHeight = 0;            // その時の高さ

    // シングルトンアクセス
    static PerfMetrics& instance();

    uint32_t totalTime() const;
    uint64_t totalNodeAllocatedBytes() const;
    void reset();
    void recordAlloc(size_t bytes, int width = 0, int height = 0);
    void recordFree(size_t bytes);
};
```

### シングルトン設計

`PerfMetrics::instance()` でグローバルにアクセス可能なシングルトンとして実装。
これにより、ImageBuffer のコンストラクタ/デストラクタから直接統計を記録できます。

```cpp
// ImageBuffer 内部で自動記録
ImageBuffer::allocate() {
    // メモリ確保
    PerfMetrics::instance().recordAlloc(bytes, width, height);
}

ImageBuffer::deallocate() {
    PerfMetrics::instance().recordFree(bytes);
    // メモリ解放
}
```

## 計測ポイント

### 時間計測（全5ノード実装済み）

| ノード | 計測範囲 |
|--------|----------|
| Source | `evaluate()` 全体（画像データコピー含む） |
| Filter | フィルタ処理のみ（上流評価除外） |
| Transform | アフィン変換処理のみ（上流評価除外） |
| Composite | ブレンド処理のみ（上流評価除外） |
| Output | `processTile()` 全体（上流評価含む） |

### ピクセル効率計測

Filter/Transformノードで入力要求サイズと出力サイズを比較：

- `requestedPixels`: 上流に要求したピクセル数（AABBサイズ）
- `usedPixels`: 出力要求サイズ（`request.width * request.height`）
- `wasteRatio`: `1.0 - usedPixels / requestedPixels`

### メモリ確保計測

#### グローバル統計（自動記録）

ImageBuffer のコンストラクタ/デストラクタで自動的に記録：

- `totalAllocatedBytes`: 累計確保バイト数
- `peakMemoryBytes`: ピークメモリ使用量（同時確保の最大値）
- `currentMemoryBytes`: 現在のメモリ使用量
- `maxAllocBytes`: 一回の最大確保サイズ
- `maxAllocWidth/Height`: その時のバッファサイズ

#### ノード別統計（明示的記録）

各ノードでImageBuffer作成時に明示的に記録：

| ノード | 計測対象 |
|--------|----------|
| Source | 交差領域コピー用バッファ |
| Filter | 作業バッファ、出力バッファ、クロップバッファ |
| Transform | 出力バッファ |
| Composite | キャンバスバッファ |

ノード別統計:
- `allocatedBytes`: このノードが確保したバイト数
- `allocCount`: 確保回数
- `maxAllocBytes/Width/Height`: 一回の最大確保サイズとそのサイズ

## 使用方法

### C++側

```cpp
Renderer renderer(sinkNode);
renderer.exec();

const PerfMetrics& metrics = renderer.getPerfMetrics();
std::cout << "Filter time: " << metrics.nodes[NodeType::Filter].time_us << "us\n";
std::cout << "Total time: " << metrics.totalTime() << "us\n";
```

### JS側（WASM）

```javascript
const metrics = evaluator.getPerfMetrics();

// nodes配列でアクセス
for (let i = 0; i < metrics.nodes.length; i++) {
    const m = metrics.nodes[i];
    console.log(`time: ${m.time_us}us, count: ${m.count}`);
    console.log(`memory: ${m.allocatedBytes} bytes (${m.allocCount} allocs)`);
    if (m.requestedPixels > 0) {
        console.log(`efficiency: ${(1 - m.wasteRatio) * 100}%`);
    }
}

// グローバルメモリ統計
console.log(`Total: ${metrics.totalAllocBytes} bytes`);
console.log(`Peak: ${metrics.peakMemoryBytes} bytes`);

// 後方互換フラットキー
console.log(metrics.filterTime, metrics.affineTime);
```

## ビルドモード

| モード | コマンド | `FLEXIMG_DEBUG` | 計測 |
|--------|---------|-----------------|------|
| リリース | `./build.sh` | 未定義 | 無効 |
| デバッグ | `./build.sh --debug` | 定義 | 有効 |

リリースビルドでは計測コードが完全に除去され、パフォーマンスへの影響はゼロです。

## 関連ファイル

| ファイル | 内容 |
|---------|------|
| `src/fleximg/perf_metrics.h` | NodeType, NodeMetrics, PerfMetrics 定義（シングルトン） |
| `src/fleximg/image_buffer.h` | ImageBuffer（自動統計記録） |
| `src/fleximg/renderer.h` | Renderer.getPerfMetrics() |
| `src/fleximg/renderer.cpp` | 各ノードの計測コード |
| `demo/bindings.cpp` | WASM bindings |
| `demo/web/app.js` | 表示UI |
