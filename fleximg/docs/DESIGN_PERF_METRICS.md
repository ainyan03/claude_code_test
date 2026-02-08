# パフォーマンス計測基盤

## 概要

ノードタイプ別にパフォーマンスメトリクスを収集し、最適化の効果を定量評価するための基盤。

**有効化**: `./build.sh --debug` でビルド（`FLEXIMG_DEBUG_PERF_METRICS` マクロ）

## データ構造

### NodeType

ノードタイプの定義は `src/fleximg/perf_metrics.h` を参照。

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

### 時間計測

| ノード | 計測範囲 |
|--------|----------|
| Renderer | `exec()` 全体時間（タイルループ、オーバーヘッド含む） |
| Source | `pullProcess()` 全体（画像データコピー含む） |
| Sink | 出力バッファへの書き込み |
| Affine | アフィン変換処理のみ（上流評価除外） |
| Composite | ブレンド処理のみ（上流評価除外） |
| Matte | マット合成処理のみ（上流評価除外） |
| Brightness | 明るさ調整処理 |
| Grayscale | グレースケール変換処理 |
| Alpha | アルファ調整処理 |
| HorizontalBlur | 水平ぼかし処理 |
| VerticalBlur | 垂直ぼかし処理 |

### Renderer時間とオーバーヘッド

`totalTime()` は **Rendererを除外** した各ノードの処理時間合計を返します。

```
オーバーヘッド = Renderer.time_us - totalTime()
```

オーバーヘッドには以下が含まれます：
- タイルループの制御
- RenderRequest の生成
- ノード間のデータ受け渡し
- RenderResponse の移動/コピー

### ピクセル効率計測

Affine/フィルタノードで入力要求サイズと出力サイズを比較：

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
| Affine | 出力バッファ |
| Composite | キャンバスバッファ |
| Matte | 出力バッファ |
| Brightness | 作業バッファ、出力バッファ |
| Grayscale | 作業バッファ、出力バッファ |
| Alpha | 作業バッファ、出力バッファ |
| HorizontalBlur | 出力バッファ |
| VerticalBlur | 行キャッシュ、列合計、出力バッファ |

ノード別統計:
- `allocatedBytes`: このノードが確保したバイト数
- `allocCount`: 確保回数
- `maxAllocBytes/Width/Height`: 一回の最大確保サイズとそのサイズ

## 使用方法

### C++側

```cpp
RendererNode renderer;
renderer.exec();

const PerfMetrics& metrics = PerfMetrics::instance();
std::cout << "Affine time: " << metrics.nodes[NodeType::Affine].time_us << "us\n";
std::cout << "Brightness time: " << metrics.nodes[NodeType::Brightness].time_us << "us\n";
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

## プラットフォーム別タイミング実装

### ESP32

ESP32では`std::chrono::high_resolution_clock`が非常に遅いため、`micros()`を使用。

```cpp
#ifdef ESP32
extern "C" unsigned long micros();
// MetricsGuard内で micros() を使用
#else
#include <chrono>
// std::chrono::high_resolution_clock を使用
#endif
```

この最適化により、ESP32での計測オーバーヘッドが約45%削減されました。

### その他のプラットフォーム

Emscripten、Linux、Windows等では`std::chrono::high_resolution_clock`を使用。

## フォーマット変換メトリクス（FormatMetrics）

### 概要

ピクセルフォーマット変換・ブレンド関数の呼び出し回数とピクセル数を計測。
PerfMetricsと同様に `FLEXIMG_DEBUG_PERF_METRICS` マクロで有効化。

### データ構造

```cpp
namespace FormatIdx {
    constexpr int RGBA8_Straight = 0;
    constexpr int RGB565_LE = 1;
    constexpr int RGB565_BE = 2;
    constexpr int RGB332 = 3;
    constexpr int RGB888 = 4;
    constexpr int BGR888 = 5;
    constexpr int Alpha8 = 6;
    constexpr int Count = 7;
}

namespace OpType {
    constexpr int ToStraight = 0;    // 各フォーマット → RGBA8_Straight
    constexpr int FromStraight = 1;  // RGBA8_Straight → 各フォーマット
    constexpr int BlendUnder = 2;    // under合成
    constexpr int Count = 3;
}

struct FormatOpEntry {
    uint32_t callCount = 0;   // 呼び出し回数
    uint64_t pixelCount = 0;  // 処理ピクセル数
};

struct FormatMetrics {
    FormatOpEntry data[FormatIdx::Count][OpType::Count];
    static FormatMetrics& instance();  // シングルトン
    void reset();
    void record(int formatIdx, int opType, int pixels);
    // 集計メソッド
    FormatOpEntry totalByOp(int opType) const;
    FormatOpEntry totalByFormat(int formatIdx) const;
    FormatOpEntry total() const;
    // スナップショット（UI変換除外用）
    void saveSnapshot(FormatOpEntry snapshot[][]) const;
    void restoreSnapshot(const FormatOpEntry snapshot[][]);
};
```

### 計測マクロ

```cpp
// 各変換関数の先頭に追加
static void rgb565le_toStraight(void* dst, const void* src, int pixelCount, const ConvertParams*) {
    FLEXIMG_FMT_METRICS(RGB565_LE, ToStraight, pixelCount);
    // 処理...
}
```

### WebUI表示

デバッグセクションの「フォーマット変換」に以下を表示:
- 操作別合計（toStraight, fromStraight, blendUnder）
- フォーマット別内訳（各フォーマットの操作別ピクセル数・呼び出し回数）

### UI変換の除外

WebUIバインディング（JS↔C++間のフォーマット変換）はパイプライン処理ではないため、
`saveSnapshot()` / `restoreSnapshot()` でメトリクスから除外。

対象箇所:
- `getSinkPreview()`: Sink出力 → RGBA8変換
- `storeImageWithFormat()`: RGBA8 → 目標フォーマット変換
- `exec()` 後のSink出力コピー: 全Sink → RGBA8変換

## 関連ファイル

| ファイル | 内容 |
|---------|------|
| `src/fleximg/core/perf_metrics.h` | NodeType, NodeMetrics, PerfMetrics 定義（シングルトン） |
| `src/fleximg/core/format_metrics.h` | FormatMetrics 定義（シングルトン） |
| `src/fleximg/image/image_buffer.h` | ImageBuffer（自動統計記録） |
| `src/fleximg/nodes/renderer_node.h` | RendererNode（パイプライン実行、メトリクスリセット） |
| `src/fleximg/image/pixel_format.h` | フォーマット変換関数（FLEXIMG_FMT_METRICSマクロ、stb-style実装部） |
| `demo/bindings.cpp` | WASM bindings（snapshot/restore） |
| `demo/web/cpp-sync-types.js` | C++同期型定義（NODE_TYPES、PIXEL_FORMATS） |
| `demo/web/app.js` | 表示UI（updateFormatMetrics等） |
