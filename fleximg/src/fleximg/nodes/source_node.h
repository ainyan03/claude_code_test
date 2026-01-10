#ifndef FLEXIMG_SOURCE_NODE_H
#define FLEXIMG_SOURCE_NODE_H

#include "../node.h"
#include "../viewport.h"
#include "../image_buffer.h"
#include "../perf_metrics.h"
#include <algorithm>
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// SourceNode - 画像入力ノード（終端）
// ========================================================================
//
// パイプラインの入力端点となるノードです。
// - 入力ポート: 0
// - 出力ポート: 1
// - 外部のViewPortを参照
//

class SourceNode : public Node {
public:
    // コンストラクタ
    SourceNode() {
        initPorts(0, 1);  // 入力0、出力1
    }

    SourceNode(const ViewPort& vp, float originX = 0, float originY = 0)
        : source_(vp), originX_(originX), originY_(originY) {
        initPorts(0, 1);
    }

    // ソース設定
    void setSource(const ViewPort& vp) { source_ = vp; }
    void setOrigin(float x, float y) { originX_ = x; originY_ = y; }

    // アクセサ
    const ViewPort& source() const { return source_; }
    float originX() const { return originX_; }
    float originY() const { return originY_; }

    const char* name() const override { return "SourceNode"; }

    // ========================================
    // プル型インターフェース
    // ========================================

    // SourceNodeは入力がないため、pullProcess()を直接オーバーライド
    RenderResult pullProcess(const RenderRequest& request) override {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto sourceStart = std::chrono::high_resolution_clock::now();
#endif

        if (!source_.isValid()) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            auto& m = PerfMetrics::instance().nodes[NodeType::Source];
            m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - sourceStart).count();
            m.count++;
#endif
            return RenderResult();
        }

        // ソース画像の基準相対座標範囲
        float imgLeft = -originX_;
        float imgTop = -originY_;
        float imgRight = imgLeft + source_.width;
        float imgBottom = imgTop + source_.height;

        // 要求範囲の基準相対座標
        float reqLeft = -request.originX;
        float reqTop = -request.originY;
        float reqRight = reqLeft + request.width;
        float reqBottom = reqTop + request.height;

        // 交差領域
        float interLeft = std::max(imgLeft, reqLeft);
        float interTop = std::max(imgTop, reqTop);
        float interRight = std::min(imgRight, reqRight);
        float interBottom = std::min(imgBottom, reqBottom);

        if (interLeft >= interRight || interTop >= interBottom) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            auto& m = PerfMetrics::instance().nodes[NodeType::Source];
            m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - sourceStart).count();
            m.count++;
#endif
            return RenderResult(ImageBuffer(), Point2f(reqLeft, reqTop));
        }

        // 交差領域をコピー
        int srcX = static_cast<int>(interLeft - imgLeft);
        int srcY = static_cast<int>(interTop - imgTop);
        int interW = static_cast<int>(interRight - interLeft);
        int interH = static_cast<int>(interBottom - interTop);

        ImageBuffer result(interW, interH, source_.formatID);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Source].recordAlloc(
            result.totalBytes(), result.width(), result.height());
#endif
        ViewPort resultView = result.view();
        view_ops::copy(resultView, 0, 0, source_, srcX, srcY, interW, interH);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& m = PerfMetrics::instance().nodes[NodeType::Source];
        m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - sourceStart).count();
        m.count++;
#endif
        return RenderResult(std::move(result), Point2f(interLeft, interTop));
    }

private:
    ViewPort source_;
    float originX_ = 0;  // 画像内の基準点X（ピクセル座標）
    float originY_ = 0;  // 画像内の基準点Y（ピクセル座標）
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SOURCE_NODE_H
