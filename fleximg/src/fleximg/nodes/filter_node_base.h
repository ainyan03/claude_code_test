#ifndef FLEXIMG_FILTER_NODE_BASE_H
#define FLEXIMG_FILTER_NODE_BASE_H

#include "../node.h"
#include "../image_buffer.h"
#include "../perf_metrics.h"
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
// - 派生クラスは process() をオーバーライドしてフィルタ処理を実装
//
// 派生クラスの実装例:
//   class BrightnessNode : public FilterNodeBase {
//   protected:
//       int nodeTypeForMetrics() const override { return NodeType::Brightness; }
//       RenderResult process(RenderResult&& input, const RenderRequest& request) override {
//           ImageBuffer working = std::move(input.buffer).toFormat(PixelFormatIDs::RGBA8_Straight);
//           // フィルタ処理...
//           return RenderResult(std::move(output), input.origin);
//       }
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
    // プル型インターフェース
    // ========================================

    // マージン追加とメトリクス記録を行い、process() に委譲
    RenderResult pullProcess(const RenderRequest& request) override {
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();

        int margin = computeInputMargin();
        RenderRequest inputReq = request.expand(margin);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        // ピクセル効率計測
        auto& metrics = PerfMetrics::instance().nodes[nodeTypeForMetrics()];
        metrics.requestedPixels += static_cast<uint64_t>(inputReq.width) * inputReq.height;
        metrics.usedPixels += static_cast<uint64_t>(request.width) * request.height;
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

    /// 入力マージン（ブラー等で拡大が必要な場合にオーバーライド）
    virtual int computeInputMargin() const { return 0; }

    /// メトリクス用ノードタイプ（派生クラスで実装）
    int nodeTypeForMetrics() const override = 0;

    // ========================================
    // process() は派生クラスでオーバーライド
    // prepare() / finalize() も必要に応じてオーバーライド可能
    // ========================================
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_FILTER_NODE_BASE_H
