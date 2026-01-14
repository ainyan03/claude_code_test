#ifndef FLEXIMG_SINK_NODE_H
#define FLEXIMG_SINK_NODE_H

#include "../core/node.h"
#include "../image/viewport.h"
#include "../operations/transform.h"
#include <algorithm>
#include <cstring>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// SinkNode - 画像出力ノード（終端）
// ========================================================================
//
// パイプラインの出力端点となるノードです。
// - 入力ポート: 1
// - 出力ポート: 0
// - 外部のViewPortに結果を書き込む
//

class SinkNode : public Node {
public:
    // コンストラクタ
    SinkNode() {
        initPorts(1, 0);  // 入力1、出力0
    }

    SinkNode(const ViewPort& vp, int_fixed8 originX = 0, int_fixed8 originY = 0)
        : target_(vp), originX_(originX), originY_(originY) {
        initPorts(1, 0);
    }

    // ターゲット設定
    void setTarget(const ViewPort& vp) { target_ = vp; }
    void setOrigin(int_fixed8 x, int_fixed8 y) { originX_ = x; originY_ = y; }

    // アクセサ
    const ViewPort& target() const { return target_; }
    ViewPort& target() { return target_; }
    int_fixed8 originX() const { return originX_; }
    int_fixed8 originY() const { return originY_; }

    // キャンバスサイズ（targetから取得）
    int16_t canvasWidth() const { return target_.width; }
    int16_t canvasHeight() const { return target_.height; }

    const char* name() const override { return "SinkNode"; }

    // ========================================
    // プッシュ型準備（アフィン情報受け取り）
    // ========================================

    bool pushPrepare(const PrepareRequest& request) override {
        // 循環参照検出
        if (pushPrepareState_ == PrepareState::Preparing) {
            pushPrepareState_ = PrepareState::CycleError;
            return false;
        }
        if (pushPrepareState_ == PrepareState::Prepared) {
            return true;
        }
        if (pushPrepareState_ == PrepareState::CycleError) {
            return false;
        }

        pushPrepareState_ = PrepareState::Preparing;

        // アフィン情報を受け取る
        if (request.hasPushAffine) {
            // 逆行列を事前計算
            invMatrix_ = inverseFixed16(request.pushAffineMatrix);
            // tx/ty を Q24.8 固定小数点で保持
            txFixed8_ = float_to_fixed8(request.pushAffineMatrix.tx);
            tyFixed8_ = float_to_fixed8(request.pushAffineMatrix.ty);
            hasAffine_ = true;
        } else {
            hasAffine_ = false;
        }

        // SinkNodeは終端なので下流への伝播なし
        pushPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // ========================================
    // プッシュ型インターフェース
    // ========================================

    // タイル単位で呼び出され、出力バッファに書き込み
    void pushProcess(RenderResult&& input,
                     const RenderRequest& request) override {
        (void)request;  // 現在は未使用

        if (!input.isValid() || !target_.isValid()) return;

        // アフィン変換が伝播されている場合はDDA処理
        if (hasAffine_) {
            pushProcessWithAffine(std::move(input));
            return;
        }

        ViewPort inputView = input.view();

        // 基準点一致ルールに基づく配置計算（固定小数点演算）
        // input.origin: 入力バッファ内での基準点位置
        // originX_/Y_: 出力バッファ内での基準点位置
        // dstX = originX_ - input.origin.x
        int dstX = from_fixed8(originX_ - input.origin.x);
        int dstY = from_fixed8(originY_ - input.origin.y);

        // クリッピング処理
        int srcX = 0, srcY = 0;
        if (dstX < 0) { srcX = -dstX; dstX = 0; }
        if (dstY < 0) { srcY = -dstY; dstY = 0; }

        int_fast32_t copyW = std::min<int_fast32_t>(inputView.width - srcX, target_.width - dstX);
        int_fast32_t copyH = std::min<int_fast32_t>(inputView.height - srcY, target_.height - dstY);

        if (copyW > 0 && copyH > 0) {
            view_ops::copy(target_, dstX, dstY, inputView, srcX, srcY,
                          static_cast<int>(copyW), static_cast<int>(copyH));
        }
    }

private:
    ViewPort target_;
    int_fixed8 originX_ = 0;  // 出力先の基準点X（固定小数点 Q24.8）
    int_fixed8 originY_ = 0;  // 出力先の基準点Y（固定小数点 Q24.8）

    // アフィン伝播用メンバ変数
    Matrix2x2_fixed16 invMatrix_;  // 逆行列（2x2部分）
    int_fixed8 txFixed8_ = 0;      // tx を Q24.8 で保持
    int_fixed8 tyFixed8_ = 0;      // ty を Q24.8 で保持
    bool hasAffine_ = false;       // アフィン変換が伝播されているか

    // ========================================
    // アフィン変換付きプッシュ処理
    // ========================================
    void pushProcessWithAffine(RenderResult&& input) {
        // 特異行列チェック
        if (!invMatrix_.valid) {
            return;
        }

        // ターゲットフォーマットに変換（フォーマットが異なる場合のみ）
        PixelFormatID targetFormat = target_.formatID;
        ImageBuffer convertedBuffer;
        ViewPort inputView;

        if (input.buffer.formatID() != targetFormat) {
            convertedBuffer = std::move(input.buffer).toFormat(targetFormat);
            inputView = convertedBuffer.view();
        } else {
            inputView = input.view();
        }

        // アフィン変換を適用してターゲットに書き込み
        applyAffine(target_, originX_, originY_,
                    inputView, input.origin.x, input.origin.y);
    }

    // ========================================
    // DDA転写テンプレート
    // ========================================
    template<size_t BytesPerPixel>
    static void copyRowDDA(
        uint8_t* dstRow,
        const uint8_t* srcData,
        int32_t srcStride,
        int32_t srcX_fixed,
        int32_t srcY_fixed,
        int32_t fixedInvA,
        int32_t fixedInvC,
        int count
    ) {
        for (int i = 0; i < count; i++) {
            uint32_t sx = static_cast<uint32_t>(srcX_fixed) >> INT_FIXED16_SHIFT;
            uint32_t sy = static_cast<uint32_t>(srcY_fixed) >> INT_FIXED16_SHIFT;

            const uint8_t* srcPixel = srcData + sy * srcStride + sx * BytesPerPixel;

            if constexpr (BytesPerPixel == 8) {
                reinterpret_cast<uint32_t*>(dstRow)[0] =
                    reinterpret_cast<const uint32_t*>(srcPixel)[0];
                reinterpret_cast<uint32_t*>(dstRow)[1] =
                    reinterpret_cast<const uint32_t*>(srcPixel)[1];
            } else if constexpr (BytesPerPixel == 4) {
                *reinterpret_cast<uint32_t*>(dstRow) =
                    *reinterpret_cast<const uint32_t*>(srcPixel);
            } else if constexpr (BytesPerPixel == 2) {
                *reinterpret_cast<uint16_t*>(dstRow) =
                    *reinterpret_cast<const uint16_t*>(srcPixel);
            } else if constexpr (BytesPerPixel == 1) {
                *dstRow = *srcPixel;
            } else {
                std::memcpy(dstRow, srcPixel, BytesPerPixel);
            }

            dstRow += BytesPerPixel;
            srcX_fixed += fixedInvA;
            srcY_fixed += fixedInvC;
        }
    }

    using CopyRowFunc = void(*)(
        uint8_t* dstRow,
        const uint8_t* srcData,
        int32_t srcStride,
        int32_t srcX_fixed,
        int32_t srcY_fixed,
        int32_t fixedInvA,
        int32_t fixedInvC,
        int count
    );

    // ========================================
    // アフィン変換実装
    // ========================================
    void applyAffine(ViewPort& dst, int_fixed8 dstOriginX, int_fixed8 dstOriginY,
                     const ViewPort& src, int_fixed8 srcOriginX, int_fixed8 srcOriginY) {
        if (!dst.isValid() || !src.isValid()) return;
        if (!invMatrix_.valid) return;

        const int outW = dst.width;
        const int outH = dst.height;

        // BytesPerPixel に応じて関数ポインタを選択
        CopyRowFunc copyRow = nullptr;
        switch (getBytesPerPixel(src.formatID)) {
            case 8: copyRow = &copyRowDDA<8>; break;
            case 4: copyRow = &copyRowDDA<4>; break;
            case 3: copyRow = &copyRowDDA<3>; break;
            case 2: copyRow = &copyRowDDA<2>; break;
            case 1: copyRow = &copyRowDDA<1>; break;
            default: return;
        }

        const int32_t fixedInvA = invMatrix_.a;
        const int32_t fixedInvB = invMatrix_.b;
        const int32_t fixedInvC = invMatrix_.c;
        const int32_t fixedInvD = invMatrix_.d;

        const int32_t dstOriginXInt = from_fixed8(dstOriginX);
        const int32_t dstOriginYInt = from_fixed8(dstOriginY);
        const int32_t srcOriginXInt = from_fixed8(srcOriginX);
        const int32_t srcOriginYInt = from_fixed8(srcOriginY);

        // 逆変換オフセットの計算
        int64_t invTx64 = -(static_cast<int64_t>(txFixed8_) * fixedInvA
                          + static_cast<int64_t>(tyFixed8_) * fixedInvB);
        int64_t invTy64 = -(static_cast<int64_t>(txFixed8_) * fixedInvC
                          + static_cast<int64_t>(tyFixed8_) * fixedInvD);
        int32_t invTxFixed = static_cast<int32_t>(invTx64 >> INT_FIXED8_SHIFT);
        int32_t invTyFixed = static_cast<int32_t>(invTy64 >> INT_FIXED8_SHIFT);

        const int32_t fixedInvTx = invTxFixed
                            - (dstOriginXInt * fixedInvA)
                            - (dstOriginYInt * fixedInvB)
                            + (srcOriginXInt << INT_FIXED16_SHIFT);
        const int32_t fixedInvTy = invTyFixed
                            - (dstOriginXInt * fixedInvC)
                            - (dstOriginYInt * fixedInvD)
                            + (srcOriginYInt << INT_FIXED16_SHIFT);

        const int32_t rowOffsetX = fixedInvB >> 1;
        const int32_t rowOffsetY = fixedInvD >> 1;
        const int32_t dxOffsetX = fixedInvA >> 1;
        const int32_t dxOffsetY = fixedInvC >> 1;

        const int32_t srcStride = src.stride;
        const uint8_t* srcData = static_cast<const uint8_t*>(src.data);

        for (int dy = 0; dy < outH; dy++) {
            int32_t rowBaseX = fixedInvB * dy + fixedInvTx + rowOffsetX;
            int32_t rowBaseY = fixedInvD * dy + fixedInvTy + rowOffsetY;

            auto [xStart, xEnd] = transform::calcValidRange(fixedInvA, rowBaseX, src.width, outW);
            auto [yStart, yEnd] = transform::calcValidRange(fixedInvC, rowBaseY, src.height, outW);
            int dxStart = std::max({0, xStart, yStart});
            int dxEnd = std::min({outW - 1, xEnd, yEnd});

            if (dxStart > dxEnd) continue;

            int32_t srcX_fixed = fixedInvA * dxStart + rowBaseX + dxOffsetX;
            int32_t srcY_fixed = fixedInvC * dxStart + rowBaseY + dxOffsetY;
            int count = dxEnd - dxStart + 1;

            uint8_t* dstRow = static_cast<uint8_t*>(dst.pixelAt(dxStart, dy));

            copyRow(dstRow, srcData, srcStride,
                    srcX_fixed, srcY_fixed, fixedInvA, fixedInvC, count);
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SINK_NODE_H
