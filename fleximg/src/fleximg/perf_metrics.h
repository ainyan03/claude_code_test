#ifndef FLEXIMG_PERF_METRICS_H
#define FLEXIMG_PERF_METRICS_H

#include "common.h"
#include <cstdint>

// ========================================================================
// デバッグ機能制御マクロ
// FLEXIMG_DEBUG が定義されている場合のみ計測機能が有効になる
// ビルド: ./build.sh --debug
// ========================================================================
#ifdef FLEXIMG_DEBUG
#define FLEXIMG_DEBUG_PERF_METRICS 1
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ノードタイプ定義（デバッグ/リリース共通）
// ========================================================================

namespace NodeType {
    // システム系
    constexpr int Renderer = 0;   // パイプライン発火点
    constexpr int Source = 1;     // 画像入力
    constexpr int Sink = 2;       // 画像出力
    // 構造系
    constexpr int Affine = 3;     // アフィン変換
    constexpr int Composite = 4;  // 合成
    // フィルタ系
    constexpr int Brightness = 5;
    constexpr int Grayscale = 6;
    constexpr int BoxBlur = 7;
    constexpr int Alpha = 8;
    constexpr int Count = 9;
}

// ========================================================================
// パフォーマンス計測構造体
// ========================================================================

#ifdef FLEXIMG_DEBUG_PERF_METRICS

// ノード別メトリクス
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

    void reset() {
        *this = NodeMetrics{};
    }

    // 不要ピクセル率（0.0〜1.0）
    float wasteRatio() const {
        if (requestedPixels == 0) return 0;
        return 1.0f - static_cast<float>(usedPixels) / static_cast<float>(requestedPixels);
    }

    // メモリ確保を記録
    void recordAlloc(size_t bytes, int width, int height) {
        allocatedBytes += bytes;
        allocCount++;
        if (bytes > maxAllocBytes) {
            maxAllocBytes = bytes;
            maxAllocWidth = width;
            maxAllocHeight = height;
        }
    }
};

struct PerfMetrics {
    NodeMetrics nodes[NodeType::Count];

    // グローバル統計（パイプライン全体）
    uint64_t totalAllocatedBytes = 0;  // 累計確保バイト数
    uint64_t peakMemoryBytes = 0;      // ピークメモリ使用量
    uint64_t currentMemoryBytes = 0;   // 現在のメモリ使用量
    uint64_t maxAllocBytes = 0;        // 一回の最大確保バイト数
    int maxAllocWidth = 0;             // その時の幅
    int maxAllocHeight = 0;            // その時の高さ

    // シングルトンインスタンス
    static PerfMetrics& instance() {
        static PerfMetrics s_instance;
        return s_instance;
    }

    void reset() {
        for (auto& n : nodes) n.reset();
        totalAllocatedBytes = 0;
        peakMemoryBytes = 0;
        currentMemoryBytes = 0;
        maxAllocBytes = 0;
        maxAllocWidth = 0;
        maxAllocHeight = 0;
    }

    // 全ノード合計の処理時間
    uint32_t totalTime() const {
        uint32_t sum = 0;
        for (const auto& n : nodes) sum += n.time_us;
        return sum;
    }

    // 全ノード合計の確保バイト数
    uint64_t totalNodeAllocatedBytes() const {
        uint64_t sum = 0;
        for (const auto& n : nodes) sum += n.allocatedBytes;
        return sum;
    }

    // メモリ確保を記録（ImageBuffer作成時に呼ぶ）
    void recordAlloc(size_t bytes, int width = 0, int height = 0) {
        totalAllocatedBytes += bytes;
        currentMemoryBytes += bytes;
        if (currentMemoryBytes > peakMemoryBytes) {
            peakMemoryBytes = currentMemoryBytes;
        }
        if (bytes > maxAllocBytes) {
            maxAllocBytes = bytes;
            maxAllocWidth = width;
            maxAllocHeight = height;
        }
    }

    // メモリ解放を記録（ImageBuffer破棄時に呼ぶ）
    void recordFree(size_t bytes) {
        if (currentMemoryBytes >= bytes) {
            currentMemoryBytes -= bytes;
        } else {
            currentMemoryBytes = 0;
        }
    }
};

#else

// リリースビルド用のダミー構造体（最小サイズ）
struct NodeMetrics {
    void reset() {}
    float wasteRatio() const { return 0; }
    void recordAlloc(size_t, int, int) {}
};

struct PerfMetrics {
    NodeMetrics nodes[NodeType::Count];
    static PerfMetrics& instance() {
        static PerfMetrics s_instance;
        return s_instance;
    }
    void reset() {}
    uint32_t totalTime() const { return 0; }
    uint64_t totalNodeAllocatedBytes() const { return 0; }
    void recordAlloc(size_t, int = 0, int = 0) {}
    void recordFree(size_t) {}
};

#endif // FLEXIMG_DEBUG_PERF_METRICS

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_PERF_METRICS_H
