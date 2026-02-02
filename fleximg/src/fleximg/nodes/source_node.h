#ifndef FLEXIMG_SOURCE_NODE_H
#define FLEXIMG_SOURCE_NODE_H

#include "../core/node.h"
#include "../core/affine_capability.h"
#include "../core/perf_metrics.h"
#include "../image/viewport.h"
#include "../image/image_buffer.h"
#include "../operations/transform.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <cstdio>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// InterpolationMode - 補間モード
// ========================================================================

enum class InterpolationMode {
    Nearest,   // 最近傍補間（デフォルト）
    Bilinear   // バイリニア補間（RGBA8888のみ対応）
};

// ========================================================================
// SourceNode - 画像入力ノード（終端）
// ========================================================================
//
// パイプラインの入力端点となるノードです。
// - 入力ポート: 0
// - 出力ポート: 1
// - 外部のViewPortを参照
//
// アフィン変換はAffineCapability Mixinから継承:
// - setMatrix(), matrix()
// - setRotation(), setScale(), setTranslation(), setRotationScale()
//
// setPosition() は setTranslation() のエイリアスとして提供（後方互換）
//

class SourceNode : public Node, public AffineCapability {
public:
    // コンストラクタ
    SourceNode() {
        initPorts(0, 1);  // 入力0、出力1
    }

    SourceNode(const ViewPort& vp, int_fixed pivotX = 0, int_fixed pivotY = 0)
        : source_(vp), pivotX_(pivotX), pivotY_(pivotY) {
        initPorts(0, 1);
    }

    // ソース設定
    void setSource(const ViewPort& vp) { source_ = vp; palette_ = PaletteData(); }
    void setSource(const ViewPort& vp, const PaletteData& palette) {
        source_ = vp;
        palette_ = palette;
    }

    // 基準点設定（pivot: 画像内のアンカーポイント）
    void setPivot(int_fixed x, int_fixed y) { pivotX_ = x; pivotY_ = y; }
    void setPivot(float x, float y) {
        pivotX_ = float_to_fixed(x);
        pivotY_ = float_to_fixed(y);
    }

    // アクセサ
    const ViewPort& source() const { return source_; }
    int_fixed pivotX() const { return pivotX_; }
    int_fixed pivotY() const { return pivotY_; }
    std::pair<float, float> getPivot() const {
        return {fixed_to_float(pivotX_), fixed_to_float(pivotY_)};
    }

    // ユーザー向けAPI: position（setTranslation のエイリアス、後方互換）
    void setPosition(float x, float y) {
        setTranslation(x, y);
    }
    std::pair<float, float> getPosition() const {
        return {localMatrix_.tx, localMatrix_.ty};
    }

    // 補間モード設定
    void setInterpolationMode(InterpolationMode mode) { interpolationMode_ = mode; }
    InterpolationMode interpolationMode() const { return interpolationMode_; }

    const char* name() const override { return "SourceNode"; }

    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: アフィン情報を受け取り、事前計算を行う
    // SourceNodeは終端なので上流への伝播なし、PrepareResponseを返す
    PrepareResponse onPullPrepare(const PrepareRequest& request) override;

    // onPullProcess: ソース画像のスキャンラインを返す
    // SourceNodeは入力がないため、上流を呼び出さずに直接処理
    RenderResponse& onPullProcess(const RenderRequest& request) override;

    // getDataRange: アフィン変換を考慮した正確なデータ範囲を返す
    // アフィン変換がある場合、AABB ではなくスキャンラインごとの正確な範囲を計算
    DataRange getDataRange(const RenderRequest& request) const override;

private:
    ViewPort source_;
    PaletteData palette_;   // パレット情報（インデックスフォーマット用、非所有）
    int_fixed pivotX_ = 0;  // 画像内の基準点X（pivot: 回転・配置の中心、固定小数点 Q16.16）
    int_fixed pivotY_ = 0;  // 画像内の基準点Y（pivot: 回転・配置の中心、固定小数点 Q16.16）
    // 注: 配置位置は localMatrix_.tx/ty で管理（AffineCapability から継承）
    InterpolationMode interpolationMode_ = InterpolationMode::Nearest;

    // アフィン伝播用メンバ変数（事前計算済み）
    AffinePrecomputed affine_;     // 逆行列・ピクセル中心オフセット
    bool hasAffine_ = false;       // アフィン変換が伝播されているか
    bool useBilinear_ = false;     // バイリニア補間を使用するか（事前計算結果）

    // フォーマット交渉（下流からの希望フォーマット）
    PixelFormatID preferredFormat_ = PixelFormatIDs::RGBA8_Straight;

    // LovyanGFX方式の範囲計算用事前計算値
    int32_t xs1_ = 0, xs2_ = 0;  // X方向の範囲境界（invAに依存）
    int32_t ys1_ = 0, ys2_ = 0;  // Y方向の範囲境界（invCに依存）
    int32_t fpWidth_ = 0;        // ソース幅（Q16.16固定小数点）
    int32_t fpHeight_ = 0;       // ソース高さ（Q16.16固定小数点）
    int32_t baseTxWithOffsets_ = 0;  // 事前計算統合: invTx + srcPivot + rowOffset + dxOffset
    int32_t baseTyWithOffsets_ = 0;  // 事前計算統合: invTy + srcPivot + rowOffset + dxOffset

    // Prepare時のorigin（Process時の差分計算用）
    int_fixed prepareOriginX_ = 0;
    int_fixed prepareOriginY_ = 0;

    // スキャンライン有効範囲を計算（pullProcessWithAffineで使用）
    // 戻り値: true=有効範囲あり, false=有効範囲なし
    // baseXWithHalf/baseYWithHalf はオプショナル出力（nullptrなら出力しない）
    bool calcScanlineRange(const RenderRequest& request,
                           int32_t& dxStart, int32_t& dxEnd,
                           int32_t* baseXWithHalf = nullptr,
                           int32_t* baseYWithHalf = nullptr) const;

    // アフィン変換付きプル処理（スキャンライン専用）
    RenderResponse& pullProcessWithAffine(const RenderRequest& request);
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// SourceNode - Template Method フック実装
// ============================================================================

PrepareResponse SourceNode::onPullPrepare(const PrepareRequest& request) {
    // 下流からの希望フォーマットを保存（将来のフォーマット最適化用）
    preferredFormat_ = request.preferredFormat;

    // Prepare時のoriginを保存（Process時の差分計算用）
    prepareOriginX_ = request.origin.x;
    prepareOriginY_ = request.origin.y;

    // AABB計算用の行列（合成済み）
    AffineMatrix combinedMatrix;
    bool hasTransform = false;

    // アフィン情報を受け取り、事前計算を行う
    // localMatrix_ が設定されている場合も、アフィン行列に合成して処理
    if (request.hasAffine || hasLocalTransform()) {
        // 行列合成: request.affineMatrix * localMatrix_
        // AffineNode直列接続と同じ解釈順序（自身の変換が先、下流の変換が後）
        if (request.hasAffine) {
            combinedMatrix = request.affineMatrix * localMatrix_;
        } else {
            combinedMatrix = localMatrix_;
        }
        hasTransform = true;

        // 逆行列とピクセル中心オフセットを計算（共通処理）
        affine_ = precomputeInverseAffine(combinedMatrix);

        if (affine_.isValid()) {
            const int32_t invA = affine_.invMatrix.a;
            const int32_t invB = affine_.invMatrix.b;
            const int32_t invC = affine_.invMatrix.c;
            const int32_t invD = affine_.invMatrix.d;

            // pivot は既に Q16.16 なのでそのまま使用
            const int32_t srcPivotXFixed16 = pivotX_;
            const int32_t srcPivotYFixed16 = pivotY_;

            // prepareOrigin を逆行列で変換（Prepare時に1回だけ計算）
            // Q16.16 × Q16.16 = Q32.32、右シフトで Q16.16 に戻す
            const int32_t prepareOffsetX = static_cast<int32_t>(
                (static_cast<int64_t>(prepareOriginX_) * invA
               + static_cast<int64_t>(prepareOriginY_) * invB) >> INT_FIXED_SHIFT);
            const int32_t prepareOffsetY = static_cast<int32_t>(
                (static_cast<int64_t>(prepareOriginX_) * invC
               + static_cast<int64_t>(prepareOriginY_) * invD) >> INT_FIXED_SHIFT);

            // バイリニア補間かどうかで有効範囲とオフセットが異なる
            const bool useBilinear = (interpolationMode_ == InterpolationMode::Bilinear)
                                   && (source_.formatID == PixelFormatIDs::RGBA8_Straight);

            if (useBilinear) {
                // バイリニア: 有効範囲は srcSize - 1 + ε
                // +1 により端ピクセル（小数部=0）も有効範囲に含める
                // 端での隣接ピクセルアクセスは copyRowDDABilinear_RGBA8888 側でクランプ
                fpWidth_ = ((source_.width - 1) << INT_FIXED_SHIFT) + 1;
                fpHeight_ = ((source_.height - 1) << INT_FIXED_SHIFT) + 1;

                xs1_ = invA + (invA < 0 ? fpWidth_ : -1);
                xs2_ = invA + (invA < 0 ? 0 : (fpWidth_ - 1));
                ys1_ = invC + (invC < 0 ? fpHeight_ : -1);
                ys2_ = invC + (invC < 0 ? 0 : (fpHeight_ - 1));

                // バイリニア: pivot の小数部を保持し、0.5 ピクセル減算（ピクセル中心基準）
                // + prepareOffset でPrepare時のoriginを事前反映
                constexpr int32_t halfPixel = 1 << (INT_FIXED_SHIFT - 1);  // 0.5 in Q16.16
                baseTxWithOffsets_ = affine_.invTxFixed
                                   + srcPivotXFixed16 - halfPixel
                                   + affine_.rowOffsetX + affine_.dxOffsetX
                                   + prepareOffsetX;
                baseTyWithOffsets_ = affine_.invTyFixed
                                   + srcPivotYFixed16 - halfPixel
                                   + affine_.rowOffsetY + affine_.dxOffsetY
                                   + prepareOffsetY;
                useBilinear_ = true;
            } else {
                // 最近傍: pivot の小数部を保持
                fpWidth_ = source_.width << INT_FIXED_SHIFT;
                fpHeight_ = source_.height << INT_FIXED_SHIFT;

                xs1_ = invA + (invA < 0 ? fpWidth_ : -1);
                xs2_ = invA + (invA < 0 ? 0 : (fpWidth_ - 1));
                ys1_ = invC + (invC < 0 ? fpHeight_ : -1);
                ys2_ = invC + (invC < 0 ? 0 : (fpHeight_ - 1));

                // dxOffset を含める（半ピクセルオフセット）
                // + prepareOffset でPrepare時のoriginを事前反映
                baseTxWithOffsets_ = affine_.invTxFixed
                                   + srcPivotXFixed16
                                   + affine_.rowOffsetX + affine_.dxOffsetX
                                   + prepareOffsetX;
                baseTyWithOffsets_ = affine_.invTyFixed
                                   + srcPivotYFixed16
                                   + affine_.rowOffsetY + affine_.dxOffsetY
                                   + prepareOffsetY;
                useBilinear_ = false;
            }

            // 平行移動のみ判定（逆行列の2x2部分が単位行列かどうか）
            // 単位行列の場合は DDA をスキップし、高速な非アフィンパスを使用
            // 非アフィンパスではピクセル中心モデルにより、平行移動・小数pivot も正しく処理
            // 注: a == -one や d == -one（反転）の場合は非アフィンパスでは処理できない
            constexpr int32_t one = 1 << INT_FIXED_SHIFT;  // 65536 (Q16.16)
            bool isTranslationOnly =
                affine_.invMatrix.a == one &&          // a == 1 のみ（-1は反転なので不可）
                affine_.invMatrix.d == one &&          // d == 1 のみ（-1は反転なので不可）
                affine_.invMatrix.b == 0 &&
                affine_.invMatrix.c == 0;

            if (isTranslationOnly) {
                hasAffine_ = false;
                // 非アフィンパスでピクセル中心モデルにより座標計算
            } else {
                hasAffine_ = true;
            }

// Note: 詳細デバッグ出力は実機では負荷が高いためコメントアウト
// #ifdef FLEXIMG_DEBUG_PERF_METRICS
//             printf("[SourceNode] isTranslationOnly=%s (invA=%d, invD=%d, invB=%d, invC=%d)\n",
//                    isTranslationOnly ? "true" : "false",
//                    affine_.invMatrix.a, affine_.invMatrix.d,
//                    affine_.invMatrix.b, affine_.invMatrix.c);
// #endif
        } else {
            // 逆行列が無効（特異行列）
            hasAffine_ = true;
        }
    } else {
        hasAffine_ = false;
    }

    // SourceNodeは終端なので上流への伝播なし
    // プルアフィン変換がある場合、出力側で必要なAABBを計算
    PrepareResponse result;
    result.status = PrepareStatus::Prepared;
    result.preferredFormat = source_.formatID;

    if (hasTransform) {
        // ソース矩形に順変換を適用して出力側のAABBを計算
        calcAffineAABB(
            source_.width, source_.height,
            {pivotX_, pivotY_},
            combinedMatrix,
            result.width, result.height, result.origin);
    } else {
        // アフィンなしの場合
        // positionを考慮してAABBの原点を計算（pullProcessと同じ座標系）
        int_fixed posOffsetX = float_to_fixed(localMatrix_.tx);
        int_fixed posOffsetY = float_to_fixed(localMatrix_.ty);
        result.width = static_cast<int16_t>(source_.width);
        result.height = static_cast<int16_t>(source_.height);
        // origin = position - pivot（画像左上隅のワールド座標）
        result.origin = {posOffsetX - pivotX_, posOffsetY - pivotY_};
    }
    return result;
}

RenderResponse& SourceNode::onPullProcess(const RenderRequest& request) {
    FLEXIMG_METRICS_SCOPE(NodeType::Source);

    if (!source_.isValid()) {
        return makeEmptyResponse(request.origin);
    }

    // アフィン変換が伝播されている場合はDDA処理
    if (hasAffine_) {
        return pullProcessWithAffine(request);
    }

    // ピクセル中心モデルによる座標計算（アフィンパスとの一貫性確保）
    // 出力ピクセル dx の中心座標 = request.origin.x + to_fixed(dx) + HALF
    // ソースピクセル = floor((中心座標 - imgLeft) / pixel_size)
    //               = from_fixed_floor(request.origin.x + HALF - imgLeft) + dx
    //               = srcBaseX + dx
    int_fixed posOffsetX = float_to_fixed(localMatrix_.tx);
    int_fixed posOffsetY = float_to_fixed(localMatrix_.ty);
    int_fixed imgLeft = posOffsetX - pivotX_;   // 画像左端のワールドX座標
    int_fixed imgTop = posOffsetY - pivotY_;    // 画像上端のワールドY座標

    // srcBase: 出力dx=0に対応するソースピクセルインデックス
    int32_t srcBaseX = from_fixed_floor(request.origin.x + INT_FIXED_HALF - imgLeft);
    int32_t srcBaseY = from_fixed_floor(request.origin.y + INT_FIXED_HALF - imgTop);

    // 有効範囲: srcBase + dx が [0, srcSize) に収まる dx の範囲
    // srcBase + dxStart >= 0  →  dxStart >= -srcBaseX
    // srcBase + dxEnd < srcSize  →  dxEnd < srcSize - srcBaseX
    auto dxStartX = std::max<int32_t>(0, -srcBaseX);
    auto dxEndX = std::min<int32_t>(request.width, source_.width - srcBaseX);
    auto dxStartY = std::max<int32_t>(0, -srcBaseY);
    auto dxEndY = std::min<int32_t>(request.height, source_.height - srcBaseY);

    if (dxStartX >= dxEndX || dxStartY >= dxEndY) {
        return makeEmptyResponse(request.origin);
    }

    int32_t validW = dxEndX - dxStartX;
    int32_t validH = dxEndY - dxStartY;

    // サブビューの参照モードImageBufferを作成（メモリ確保なし）
    auto srcX = static_cast<int_fast16_t>(srcBaseX + dxStartX);
    auto srcY = static_cast<int_fast16_t>(srcBaseY + dxStartY);
    ImageBuffer result(view_ops::subView(source_, srcX, srcY,
                                         static_cast<int_fast16_t>(validW),
                                         static_cast<int_fast16_t>(validH)));
    // パレット情報を出力ImageBufferに設定
    if (palette_) {
        result.setPalette(palette_);
    }

    // origin = リクエストグリッドに整列（アフィンパスと同形式）
    Point adjustedOrigin = {
        request.origin.x + to_fixed(dxStartX),
        request.origin.y + to_fixed(dxStartY)
    };
    return makeResponse(std::move(result), adjustedOrigin);
}

// ============================================================================
// SourceNode - private ヘルパーメソッド実装
// ============================================================================

// スキャンライン有効範囲を計算（getDataRange/pullProcessWithAffineで共用）
// 戻り値: true=有効範囲あり, false=有効範囲なし
bool SourceNode::calcScanlineRange(const RenderRequest& request,
                                   int32_t& dxStart, int32_t& dxEnd,
                                   int32_t* outBaseX, int32_t* outBaseY) const {
    // 特異行列チェック
    if (!affine_.isValid()) {
        return false;
    }

    // LovyanGFX/pixel_image.hpp 方式: 事前計算済み境界値を使った範囲計算
    const int32_t invA = affine_.invMatrix.a;
    const int32_t invB = affine_.invMatrix.b;
    const int32_t invC = affine_.invMatrix.c;
    const int32_t invD = affine_.invMatrix.d;

    // Prepare時のoriginからの差分（ピクセル単位の整数）
    // RendererNodeはピクセル単位でタイル分割するため、差分は常に整数
    const int32_t deltaX = from_fixed(request.origin.x - prepareOriginX_);
    const int32_t deltaY = from_fixed(request.origin.y - prepareOriginY_);

    // 整数 × Q16.16 = Q16.16（int32_t範囲内）
    // baseTxWithOffsets_ は Prepare時のoriginに対応した値として事前計算済み
    const int32_t baseX = baseTxWithOffsets_ + deltaX * invA + deltaY * invB;
    const int32_t baseY = baseTyWithOffsets_ + deltaX * invC + deltaY * invD;

    int32_t left = 0;
    int32_t right = request.width;

    if (invA) {
        left  = std::max(left, (xs1_ - baseX) / invA);
        right = std::min(right, (xs2_ - baseX) / invA);
    } else if (static_cast<uint32_t>(baseX) >= static_cast<uint32_t>(fpWidth_)) {
        left = 1;
        right = 0;
    }

    if (invC) {
        left  = std::max(left, (ys1_ - baseY) / invC);
        right = std::min(right, (ys2_ - baseY) / invC);
    } else if (static_cast<uint32_t>(baseY) >= static_cast<uint32_t>(fpHeight_)) {
        left = 1;
        right = 0;
    }

    dxStart = left;
    dxEnd = right - 1;  // right は排他的なので -1

    // DDA用ベース座標を出力（オプショナル）
    if (outBaseX) *outBaseX = baseX;
    if (outBaseY) *outBaseY = baseY;

    return dxStart <= dxEnd;
}

// getDataRange: バウンディングボックスベースの概算範囲を返す
// アフィン変換時も厳密なDDA計算は行わず、AABBで近似
// 実際の有効範囲はpullProcess時に確定する
DataRange SourceNode::getDataRange(const RenderRequest& request) const {
    return prepareResponse_.getDataRange(request);
}

// アフィン変換付きプル処理（スキャンライン専用）
// 前提: request.height == 1（RendererNodeはスキャンライン単位で処理）
// 有効範囲のみのバッファを返し、範囲外の0データを下流に送らない
RenderResponse& SourceNode::pullProcessWithAffine(const RenderRequest& request) {
    // スキャンライン有効範囲を計算
    int32_t dxStart = 0, dxEnd = 0, baseX = 0, baseY = 0;
    if (!calcScanlineRange(request, dxStart, dxEnd, &baseX, &baseY)) {
        return makeEmptyResponse(request.origin);
    }

    // originを有効範囲に合わせて調整
    // dxStart分だけ右にオフセット（バッファ左端のワールド座標）
    Point adjustedOrigin = {
        request.origin.x + to_fixed(dxStart),
        request.origin.y
    };

    // 空のResponseを取得し、bufferSetに直接バッファを作成（ムーブなし）
    int validWidth = dxEnd - dxStart + 1;
    RenderResponse& resp = makeEmptyResponse(adjustedOrigin);
    ImageBuffer* output = resp.bufferSet.createBuffer(
        validWidth, 1, source_.formatID, InitPolicy::Uninitialized, 0);

    if (!output) {
        return resp;  // バッファ作成失敗時は空のResponseを返す
    }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().nodes[NodeType::Source].recordAlloc(
        output->totalBytes(), output->width(), output->height());
#endif

    // DDA転写（1行のみ）
    const int32_t invA = affine_.invMatrix.a;
    const int32_t invC = affine_.invMatrix.c;
    int32_t srcX_fixed = invA * dxStart + baseX;
    int32_t srcY_fixed = invC * dxStart + baseY;

    void* dstRow = output->data();

    if (useBilinear_) {
        // バイリニア補間（RGBA8888専用、他フォーマットは最近傍フォールバック）
        view_ops::copyRowDDABilinear(dstRow, source_, validWidth,
            srcX_fixed, srcY_fixed, invA, invC);
    } else {
        // 最近傍補間（BPP分岐は関数内部で実施）
        // view_ops::copyRowDDA(dstRow, source_, validWidth,
        //     srcX_fixed, srcY_fixed, invA, invC);

        // DDAParam を構築（srcWidth/srcHeight/weightsはcopyRowDDAでは使用しない）
        DDAParam param = { source_.stride, 0, 0, srcX_fixed, srcY_fixed, invA, invC, nullptr };

        // フォーマットの関数ポインタを呼び出し
       if (source_.formatID && source_.formatID->copyRowDDA) {
            source_.formatID->copyRowDDA(
                static_cast<uint8_t*>(dstRow),
                static_cast<const uint8_t*>(source_.data),
                validWidth,
                &param
            );
       }
    }

    // パレット情報を出力ImageBufferに設定
    if (palette_) {
        output->setPalette(palette_);
    }

    return resp;
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_SOURCE_NODE_H
