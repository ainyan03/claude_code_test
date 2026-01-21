#ifndef FLEXIMG_PERF_METRICS_H
#define FLEXIMG_PERF_METRICS_H

#include "common.h"
#include <cstdint>

// ESP32向けには軽量なmicros()を使用、それ以外はstd::chronoを使用
#ifdef ESP32
extern "C" unsigned long micros();
#else
#include <chrono>
#endif

// ========================================================================
// デバッグ機能制御マクロ
// FLEXIMG_DEBUG が定義されている場合のみ計測機能が有効になる
// ビルド: ./build.sh --debug
// ========================================================================
#ifdef FLEXIMG_DEBUG
#define FLEXIMG_DEBUG_PERF_METRICS 1
#endif

namespace FLEXIMG_NAMESPACE {
namespace core {

// ========================================================================
// ノードタイプ定義（デバッグ/リリース共通）
// ========================================================================
//
// 重要: demo/web/app.js の NODE_TYPES と同期を維持すること
//
// 新規ノード追加時の手順:
//   1. ここに新しいノードタイプを追加（連番で）
//   2. Count を更新（最後のノードタイプ + 1）
//   3. demo/web/app.js の NODE_TYPES にも同じ index で追加
//   4. 該当ノードの nodeTypeForMetrics() が正しい値を返すことを確認
//
// ========================================================================

namespace NodeType {
    // システム系
    constexpr int Renderer = 0;    // パイプライン発火点（exec()全体時間を記録）
    constexpr int Source = 1;      // 画像入力
    constexpr int Sink = 2;        // 画像出力
    constexpr int Distributor = 3; // 分配（1入力→N出力）
    // 構造系
    constexpr int Affine = 4;      // アフィン変換
    constexpr int Composite = 5;   // 合成（N入力→1出力）
    // フィルタ系
    constexpr int Brightness = 6;
    constexpr int Grayscale = 7;
    // 8: 廃止（旧BoxBlur）
    constexpr int Alpha = 9;
    constexpr int HorizontalBlur = 10;
    constexpr int VerticalBlur = 11;
    // 特殊ソース系
    constexpr int NinePatch = 12;  // 9patch画像
    // 合成系
    constexpr int Matte = 13;      // マット合成（3入力）

    constexpr int Count = 14;
}

// コンパイル時チェック: 最後のノードタイプ + 1 == Count
// ノード追加時に Count の更新を忘れるとここでエラーになる
static_assert(NodeType::Matte + 1 == NodeType::Count,
              "NodeType::Count must equal last node type + 1. "
              "Also update demo/web/app.js NODE_TYPES.");
static_assert(NodeType::VerticalBlur == 11,
              "HorizontalBlur=10, VerticalBlur=11 must match app.js NODE_TYPES.");

// ========================================================================
// パフォーマンス計測構造体
// ========================================================================

#ifdef FLEXIMG_DEBUG_PERF_METRICS

// ノード別メトリクス
struct NodeMetrics {
    uint32_t time_us = 0;         // 処理時間（マイクロ秒）
    int count = 0;                // 呼び出し回数
    uint32_t requestedPixels = 0; // 上流に要求したピクセル数
    uint32_t usedPixels = 0;      // 実際に使用したピクセル数
    uint32_t theoreticalMinPixels = 0; // 理論最小ピクセル数（分割時の推定値）
    uint32_t allocatedBytes = 0;  // このノードが確保したバイト数
    int allocCount = 0;           // 確保回数
    uint32_t maxAllocBytes = 0;   // 一回の最大確保バイト数
    int maxAllocWidth = 0;        // その時の幅
    int maxAllocHeight = 0;       // その時の高さ

    void reset() {
        *this = NodeMetrics{};
    }

    // 現在のピクセル効率（0.0〜1.0）: usedPixels / requestedPixels
    float pixelEfficiency() const {
        if (requestedPixels == 0) return 1.0f;
        return static_cast<float>(usedPixels) / static_cast<float>(requestedPixels);
    }

    // 不要ピクセル率（0.0〜1.0）
    float wasteRatio() const {
        if (requestedPixels == 0) return 0;
        return 1.0f - static_cast<float>(usedPixels) / static_cast<float>(requestedPixels);
    }

    // 分割時の推定効率（0.0〜1.0）: theoreticalMinPixels / requestedPixels
    // 分割により理論上達成可能な効率（通常 ~50%）
    float splitEfficiencyEstimate() const {
        if (requestedPixels == 0) return 1.0f;
        return static_cast<float>(theoreticalMinPixels) / static_cast<float>(requestedPixels);
    }

    // メモリ確保を記録
    void recordAlloc(size_t bytes, int width, int height) {
        allocatedBytes += static_cast<uint32_t>(bytes);
        allocCount++;
        if (static_cast<uint32_t>(bytes) > maxAllocBytes) {
            maxAllocBytes = static_cast<uint32_t>(bytes);
            maxAllocWidth = width;
            maxAllocHeight = height;
        }
    }
};

struct PerfMetrics {
    NodeMetrics nodes[NodeType::Count];

    // グローバル統計（パイプライン全体）
    uint32_t totalAllocatedBytes = 0;  // 累計確保バイト数
    uint32_t peakMemoryBytes = 0;      // ピークメモリ使用量
    uint32_t currentMemoryBytes = 0;   // 現在のメモリ使用量
    uint32_t maxAllocBytes = 0;        // 一回の最大確保バイト数
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

    // 全ノード合計の処理時間（Renderer除外）
    // Rendererはexec()全体時間を記録するため、合計から除外
    uint32_t totalTime() const {
        uint32_t sum = 0;
        for (int i = 0; i < NodeType::Count; ++i) {
            if (i == NodeType::Renderer) continue;  // exec()全体時間は除外
            sum += nodes[i].time_us;
        }
        return sum;
    }

    // 全ノード合計の確保バイト数
    uint32_t totalNodeAllocatedBytes() const {
        uint32_t sum = 0;
        for (const auto& n : nodes) sum += n.allocatedBytes;
        return sum;
    }

    // メモリ確保を記録（ImageBuffer作成時に呼ぶ）
    void recordAlloc(size_t bytes, int width = 0, int height = 0) {
        uint32_t b = static_cast<uint32_t>(bytes);
        totalAllocatedBytes += b;
        currentMemoryBytes += b;
        if (currentMemoryBytes > peakMemoryBytes) {
            peakMemoryBytes = currentMemoryBytes;
        }
        if (b > maxAllocBytes) {
            maxAllocBytes = b;
            maxAllocWidth = width;
            maxAllocHeight = height;
        }
    }

    // メモリ解放を記録（ImageBuffer破棄時に呼ぶ）
    void recordFree(size_t bytes) {
        uint32_t b = static_cast<uint32_t>(bytes);
        if (currentMemoryBytes >= b) {
            currentMemoryBytes -= b;
        } else {
            currentMemoryBytes = 0;
        }
    }
};

// ========================================================================
// RAII スタイルのメトリクス計測ガード
// ========================================================================
//
// 使用例:
//   RenderResult process(...) override {
//       FLEXIMG_METRICS_SCOPE(nodeTypeForMetrics());
//       // ... 処理 ...
//       return result;
//   }
//
// スコープを抜けると自動的に経過時間とカウントが記録される。
// リリースビルドではノーオペレーションになる。
//
class MetricsGuard {
public:
    explicit MetricsGuard(int nodeType)
        : nodeType_(nodeType)
#ifdef ESP32
        , start_(micros())
#else
        , start_(std::chrono::high_resolution_clock::now())
#endif
    {}

    ~MetricsGuard() {
#ifdef ESP32
        uint32_t elapsed = micros() - start_;
#else
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start_).count();
#endif
        auto& metrics = PerfMetrics::instance().nodes[nodeType_];
        metrics.time_us += static_cast<uint32_t>(elapsed);
        metrics.count++;
    }

    // コピー・ムーブ禁止
    MetricsGuard(const MetricsGuard&) = delete;
    MetricsGuard& operator=(const MetricsGuard&) = delete;
    MetricsGuard(MetricsGuard&&) = delete;
    MetricsGuard& operator=(MetricsGuard&&) = delete;

private:
    int nodeType_;
#ifdef ESP32
    uint32_t start_;
#else
    std::chrono::high_resolution_clock::time_point start_;
#endif
};

#define FLEXIMG_METRICS_SCOPE(nodeType) \
    ::FLEXIMG_NAMESPACE::core::MetricsGuard _metricsGuard##__LINE__(nodeType)

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
    uint32_t totalNodeAllocatedBytes() const { return 0; }
    void recordAlloc(size_t, int = 0, int = 0) {}
    void recordFree(size_t) {}
};

// リリースビルド用: メトリクス計測マクロは何もしない
#define FLEXIMG_METRICS_SCOPE(nodeType) ((void)0)

#endif // FLEXIMG_DEBUG_PERF_METRICS

} // namespace core

// [DEPRECATED] 後方互換性のため親名前空間に公開。将来廃止予定。
// 新規コードでは core:: プレフィックスを使用してください。
namespace NodeType = core::NodeType;
using core::NodeMetrics;
using core::PerfMetrics;

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_PERF_METRICS_H
