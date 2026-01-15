#ifndef FLEXIMG_SOURCE_NODE_H
#define FLEXIMG_SOURCE_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/viewport.h"
#include "../image/image_buffer.h"
#include "../operations/transform.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#include <cstdio>
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

    // ソース設定
    void setSource(const ViewPort& vp) { source_ = vp; }
    void setOrigin(int_fixed8 x, int_fixed8 y) { originX_ = x; originY_ = y; }

    // アクセサ
    const ViewPort& source() const { return source_; }
    int_fixed8 originX() const { return originX_; }
    int_fixed8 originY() const { return originY_; }

    const char* name() const override { return "SourceNode"; }

    // ========================================
    // PrepareRequest対応（循環検出+アフィン伝播）
    // ========================================

    bool pullPrepare(const PrepareRequest& request) override {
        bool shouldContinue;
        if (!checkPrepareState(pullPrepareState_, shouldContinue)) {
            return false;
        }
        if (!shouldContinue) {
            return true;  // DAG共有ノード: スキップ
        }

        // アフィン情報を受け取り、事前計算を行う
        if (request.hasAffine) {
            // 逆行列とピクセル中心オフセットを計算（共通処理）
            affine_ = precomputeInverseAffine(request.affineMatrix);

            if (affine_.isValid()) {
                // LovyanGFX方式の範囲計算用事前計算
                fpWidth_ = source_.width << INT_FIXED16_SHIFT;
                fpHeight_ = source_.height << INT_FIXED16_SHIFT;

                const int32_t invA = affine_.invMatrix.a;
                const int32_t invC = affine_.invMatrix.c;

                // 境界値の事前計算（符号によって異なる）
                xs1_ = invA + (invA < 0 ? fpWidth_ : -1);
                xs2_ = invA + (invA < 0 ? 0 : (fpWidth_ - 1));
                ys1_ = invC + (invC < 0 ? fpHeight_ : -1);
                ys2_ = invC + (invC < 0 ? 0 : (fpHeight_ - 1));

                // オフセット統合（process時の加算を削減）
                // baseTxWithOffsets = invTx + srcOrigin + rowOffset + dxOffset
                const int32_t srcOriginXInt = from_fixed8(originX_);
                const int32_t srcOriginYInt = from_fixed8(originY_);
                baseTxWithOffsets_ = affine_.invTxFixed
                                   + (srcOriginXInt << INT_FIXED16_SHIFT)
                                   + affine_.rowOffsetX + affine_.dxOffsetX;
                baseTyWithOffsets_ = affine_.invTyFixed
                                   + (srcOriginYInt << INT_FIXED16_SHIFT)
                                   + affine_.rowOffsetY + affine_.dxOffsetY;
            }
            hasAffine_ = true;
        } else {
            hasAffine_ = false;
        }

        // SourceNodeは終端なので上流への伝播なし
        pullPrepareState_ = PrepareState::Prepared;
        return true;
    }

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

        // アフィン変換が伝播されている場合はDDA処理
        if (hasAffine_) {
            return pullProcessWithAffine(request);
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

    // アフィン伝播用メンバ変数（事前計算済み）
    AffinePrecomputed affine_;     // 逆行列・ピクセル中心オフセット
    bool hasAffine_ = false;       // アフィン変換が伝播されているか

    // LovyanGFX方式の範囲計算用事前計算値
    int32_t xs1_ = 0, xs2_ = 0;  // X方向の範囲境界（invAに依存）
    int32_t ys1_ = 0, ys2_ = 0;  // Y方向の範囲境界（invCに依存）
    int32_t fpWidth_ = 0;        // ソース幅（Q16.16固定小数点）
    int32_t fpHeight_ = 0;       // ソース高さ（Q16.16固定小数点）
    int32_t baseTxWithOffsets_ = 0;  // 事前計算統合: invTx + srcOrigin + rowOffset + dxOffset
    int32_t baseTyWithOffsets_ = 0;  // 事前計算統合: invTy + srcOrigin + rowOffset + dxOffset

    // ========================================
    // アフィン変換付きプル処理（スキャンライン専用）
    // ========================================
    // 前提: request.height == 1（RendererNodeはスキャンライン単位で処理）
    // 有効範囲のみのバッファを返し、範囲外の0データを下流に送らない
    //
    RenderResult pullProcessWithAffine(const RenderRequest& request) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto sourceStart = std::chrono::high_resolution_clock::now();
#endif

        // 特異行列チェック
        if (!affine_.isValid()) {
            return RenderResult(ImageBuffer(), request.origin);
        }

        // dstOrigin分を計算
        const int32_t dstOriginXInt = from_fixed8(request.origin.x);
        const int32_t dstOriginYInt = from_fixed8(request.origin.y);

        // LovyanGFX/pixel_image.hpp 方式: 事前計算済み境界値を使った範囲計算
        const int32_t invA = affine_.invMatrix.a;
        const int32_t invB = affine_.invMatrix.b;
        const int32_t invC = affine_.invMatrix.c;
        const int32_t invD = affine_.invMatrix.d;

        // 事前計算統合版（オフセット加算を削減）
        // baseWithHalf = baseTx + rowOffsetX + dxOffsetX - origin*inv
        const int32_t baseXWithHalf = baseTxWithOffsets_
                            - (dstOriginXInt * invA)
                            - (dstOriginYInt * invB);
        const int32_t baseYWithHalf = baseTyWithOffsets_
                            - (dstOriginXInt * invC)
                            - (dstOriginYInt * invD);

        int dxStart, dxEnd;

        int32_t left = 0;
        int32_t right = request.width;

        if (invA) {
            left  = std::max(left, (xs1_ - baseXWithHalf) / invA);
            right = std::min(right, (xs2_ - baseXWithHalf) / invA);
        } else if (static_cast<uint32_t>(baseXWithHalf) >= static_cast<uint32_t>(fpWidth_)) {
            // invA == 0 で baseXWithHalf がソース範囲外の場合は描画不可
            left = 1;
            right = 0;
        }

        if (invC) {
            left  = std::max(left, (ys1_ - baseYWithHalf) / invC);
            right = std::min(right, (ys2_ - baseYWithHalf) / invC);
        } else if (static_cast<uint32_t>(baseYWithHalf) >= static_cast<uint32_t>(fpHeight_)) {
            // invC == 0 で baseYWithHalf がソース範囲外の場合は描画不可
            left = 1;
            right = 0;
        }

        dxStart = left;
        dxEnd = right - 1;  // right は排他的なので -1

        // 有効ピクセルがない場合
        if (dxStart > dxEnd) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            auto& m = PerfMetrics::instance().nodes[NodeType::Source];
            m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - sourceStart).count();
            m.count++;
#endif
            return RenderResult(ImageBuffer(), request.origin);
        }

        // 有効範囲のみのバッファを作成
        int validWidth = dxEnd - dxStart + 1;
        ImageBuffer output(validWidth, 1, source_.formatID);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Source].recordAlloc(
            output.totalBytes(), output.width(), output.height());
#endif

        // DDA転写（1行のみ）
        // srcX_fixed = invA * dxStart + baseXWithHalf（baseXWithHalfにdxOffsetXは含まれている）
        int32_t srcX_fixed = invA * dxStart + baseXWithHalf;
        int32_t srcY_fixed = invC * dxStart + baseYWithHalf;

        uint8_t* dstRow = static_cast<uint8_t*>(output.data());
        const uint8_t* srcData = static_cast<const uint8_t*>(source_.data);

        switch (getBytesPerPixel(source_.formatID)) {
            case 8:
                transform::copyRowDDA<8>(dstRow, srcData, source_.stride,
                    srcX_fixed, srcY_fixed, affine_.invMatrix.a, affine_.invMatrix.c, validWidth);
                break;
            case 4:
                transform::copyRowDDA<4>(dstRow, srcData, source_.stride,
                    srcX_fixed, srcY_fixed, affine_.invMatrix.a, affine_.invMatrix.c, validWidth);
                break;
            case 3:
                transform::copyRowDDA<3>(dstRow, srcData, source_.stride,
                    srcX_fixed, srcY_fixed, affine_.invMatrix.a, affine_.invMatrix.c, validWidth);
                break;
            case 2:
                transform::copyRowDDA<2>(dstRow, srcData, source_.stride,
                    srcX_fixed, srcY_fixed, affine_.invMatrix.a, affine_.invMatrix.c, validWidth);
                break;
            case 1:
                transform::copyRowDDA<1>(dstRow, srcData, source_.stride,
                    srcX_fixed, srcY_fixed, affine_.invMatrix.a, affine_.invMatrix.c, validWidth);
                break;
            default:
                break;
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& m = PerfMetrics::instance().nodes[NodeType::Source];
        m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - sourceStart).count();
        m.count++;
#endif

        // originを有効範囲に合わせて調整
        // dxStart分だけ左にオフセット
        Point adjustedOrigin = {
            request.origin.x - to_fixed8(dxStart),
            request.origin.y
        };

        return RenderResult(std::move(output), adjustedOrigin);
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SOURCE_NODE_H
