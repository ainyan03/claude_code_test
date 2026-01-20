#ifndef FLEXIMG_FILTER_NODE_BASE_H
#define FLEXIMG_FILTER_NODE_BASE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../operations/filters.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// FilterNodeBase - フィルタノード基底クラス
// ========================================================================
//
// フィルタ系ノードの共通基底クラスです。
// - 入力: 1ポート
// - 出力: 1ポート
// - スキャンライン必須仕様（height=1）前提で動作
//
// 派生クラスの実装:
//   - getFilterFunc() でフィルタ関数を返す
//   - params_ にパラメータを設定
//   - nodeTypeForMetrics() でメトリクス用ノードタイプを返す
//
// 派生クラスの実装例:
//   class BrightnessNode : public FilterNodeBase {
//   public:
//       void setAmount(float v) { params_.value1 = v; }
//       float amount() const { return params_.value1; }
//   protected:
//       filters::LineFilterFunc getFilterFunc() const override {
//           return &filters::brightness_line;
//       }
//       int nodeTypeForMetrics() const override { return NodeType::Brightness; }
//       const char* name() const override { return "BrightnessNode"; }
//   };
//

class FilterNodeBase : public Node {
public:
    FilterNodeBase() {
        initPorts(1, 1);  // 入力1、出力1
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "FilterNodeBase"; }

    // ========================================
    // Template Method フック
    // ========================================

    // onPullProcess: マージン追加とメトリクス記録を行い、process() に委譲
    RenderResult onPullProcess(const RenderRequest& request) override {
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();

        int margin = computeInputMargin();
        RenderRequest inputReq = request.expand(margin);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // ピクセル効率計測
        auto& metrics = PerfMetrics::instance().nodes[nodeTypeForMetrics()];
        metrics.requestedPixels += static_cast<uint64_t>(inputReq.width) * static_cast<uint64_t>(inputReq.height);
        metrics.usedPixels += static_cast<uint64_t>(request.width) * static_cast<uint64_t>(request.height);
#endif

        RenderResult input = upstream->pullProcess(inputReq);
        if (!input.isValid()) return input;

        // process() を呼ぶ（Node基底クラスの設計に沿う）
        return process(std::move(input), request);
    }

protected:
    // ========================================
    // 派生クラスがオーバーライドするフック
    // ========================================

    /// ラインフィルタ関数を返す（派生クラスで実装）
    virtual filters::LineFilterFunc getFilterFunc() const = 0;

    /// 入力マージン（ブラー等で拡大が必要な場合にオーバーライド）
    virtual int computeInputMargin() const { return 0; }

    /// メトリクス用ノードタイプ（派生クラスで実装）
    int nodeTypeForMetrics() const override = 0;

    // ========================================
    // process() 共通実装
    // ========================================
    //
    // スキャンライン必須仕様（height=1）前提の共通処理:
    // 1. RGBA8_Straight形式に変換
    // 2. ラインフィルタ関数を適用
    // 3. パフォーマンス計測（デバッグビルド時）
    //

    RenderResult process(RenderResult&& input,
                        const RenderRequest& request) override {
        (void)request;  // スキャンライン必須仕様では未使用

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto start = std::chrono::high_resolution_clock::now();
#endif

        // 入力をRGBA8_Straightに変換（メトリクス記録付き）
        ImageBuffer working = convertFormat(std::move(input.buffer), PixelFormatIDs::RGBA8_Straight);
        ViewPort workingView = working.view();

        // ラインフィルタを適用（height=1前提）
        uint8_t* row = static_cast<uint8_t*>(workingView.data);
        getFilterFunc()(row, workingView.width, params_);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[nodeTypeForMetrics()];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - start).count();
        metrics.count++;
#endif

        return RenderResult(std::move(working), input.origin);
    }

    // ========================================
    // パラメータ（派生クラスからアクセス可能）
    // ========================================

    filters::LineFilterParams params_;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_FILTER_NODE_BASE_H
