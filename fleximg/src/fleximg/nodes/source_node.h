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

    SourceNode(const ViewPort& vp, int_fixed8 originX = 0, int_fixed8 originY = 0)
        : source_(vp), originX_(originX), originY_(originY) {
        initPorts(0, 1);
    }

    // 移行用コンストラクタ（float引数、最終的に削除予定）
    SourceNode(const ViewPort& vp, float originX, float originY)
        : source_(vp)
        , originX_(float_to_fixed8(originX))
        , originY_(float_to_fixed8(originY)) {
        initPorts(0, 1);
    }

    // ソース設定
    void setSource(const ViewPort& vp) { source_ = vp; }
    void setOrigin(int_fixed8 x, int_fixed8 y) { originX_ = x; originY_ = y; }

    // アクセサ
    const ViewPort& source() const { return source_; }
    int_fixed8 originX() const { return originX_; }
    int_fixed8 originY() const { return originY_; }

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

        // ソース画像の基準相対座標範囲（固定小数点 Q24.8）
        int_fixed8 imgLeft = -originX_;
        int_fixed8 imgTop = -originY_;
        int_fixed8 imgRight = imgLeft + to_fixed8(source_.width);
        int_fixed8 imgBottom = imgTop + to_fixed8(source_.height);

        // 要求範囲の基準相対座標（固定小数点 Q24.8）
        int_fixed8 reqLeft = -request.origin.x;
        int_fixed8 reqTop = -request.origin.y;
        int_fixed8 reqRight = reqLeft + to_fixed8(request.width);
        int_fixed8 reqBottom = reqTop + to_fixed8(request.height);

        // 交差領域
        int_fixed8 interLeft = std::max(imgLeft, reqLeft);
        int_fixed8 interTop = std::max(imgTop, reqTop);
        int_fixed8 interRight = std::min(imgRight, reqRight);
        int_fixed8 interBottom = std::min(imgBottom, reqBottom);

        if (interLeft >= interRight || interTop >= interBottom) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            auto& m = PerfMetrics::instance().nodes[NodeType::Source];
            m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - sourceStart).count();
            m.count++;
#endif
            // バッファ内基準点位置 = request.origin
            return RenderResult(ImageBuffer(), request.origin);
        }

        // 交差領域のサブビューを参照モードで返す（コピーなし）
        // 開始位置は floor、終端位置は ceil で計算（タイル境界でのピクセル欠落を防ぐ）
        int srcX = from_fixed8_floor(interLeft - imgLeft);
        int srcY = from_fixed8_floor(interTop - imgTop);
        int srcEndX = from_fixed8_ceil(interRight - imgLeft);
        int srcEndY = from_fixed8_ceil(interBottom - imgTop);
        int interW = srcEndX - srcX;
        int interH = srcEndY - srcY;

        // サブビューの参照モードImageBufferを作成（メモリ確保なし）
        ImageBuffer result(view_ops::subView(source_, srcX, srcY, interW, interH));

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& m = PerfMetrics::instance().nodes[NodeType::Source];
        m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - sourceStart).count();
        m.count++;
#endif
        // バッファ内基準点位置 = -interLeft, -interTop
        return RenderResult(std::move(result), Point{-interLeft, -interTop});
    }

private:
    ViewPort source_;
    int_fixed8 originX_ = 0;  // 画像内の基準点X（固定小数点 Q24.8）
    int_fixed8 originY_ = 0;  // 画像内の基準点Y（固定小数点 Q24.8）
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SOURCE_NODE_H
