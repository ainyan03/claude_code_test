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

    // カラーキー設定（アルファなしフォーマットで特定色を透明化）
    void setColorKey(uint32_t colorKeyRGBA8, uint32_t replaceRGBA8 = 0) {
        colorKeyRGBA8_ = colorKeyRGBA8;
        colorKeyReplace_ = replaceRGBA8;
    }
    void clearColorKey() {
        colorKeyRGBA8_ = 0;
        colorKeyReplace_ = 0;
    }

    // 補間モード設定
    void setInterpolationMode(InterpolationMode mode) { interpolationMode_ = mode; }
    InterpolationMode interpolationMode() const { return interpolationMode_; }

    // エッジフェードアウト設定（バイリニア補間時のみ有効）
    // フェード有効な辺では出力範囲が0.5ピクセル拡張され、境界がなめらかに透明化
    // フェード無効な辺では出力範囲はNearestと同じ、境界ピクセルはクランプ
    void setEdgeFade(uint8_t flags) { edgeFadeFlags_ = flags; }
    uint8_t edgeFade() const { return edgeFadeFlags_; }

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

    // getDataRange: スキャンライン単位の正確なデータ範囲を返す
    // アフィン変換がある場合、calcScanlineRangeで厳密な有効範囲を計算
    // AABB上限が必要な場合は getDataRangeBounds() を使用
    DataRange getDataRange(const RenderRequest& request) const override;

private:
    ViewPort source_;
    PaletteData palette_;   // パレット情報（インデックスフォーマット用、非所有）
    int_fixed pivotX_ = 0;  // 画像内の基準点X（pivot: 回転・配置の中心、固定小数点 Q16.16）
    int_fixed pivotY_ = 0;  // 画像内の基準点Y（pivot: 回転・配置の中心、固定小数点 Q16.16）
    // 注: 配置位置は localMatrix_.tx/ty で管理（AffineCapability から継承）
    InterpolationMode interpolationMode_ = InterpolationMode::Nearest;
    uint8_t edgeFadeFlags_ = EdgeFade_All;  // デフォルト: 全辺フェードアウト有効
    uint32_t colorKeyRGBA8_ = 0;    // カラーキー比較値（RGBA8、alpha込み）
    uint32_t colorKeyReplace_ = 0;  // カラーキー差し替え値（通常は透明黒0）

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

    // getDataRangeキャッシュ（同一スキャンラインでの重複計算を回避）
    // NinePatchSourceNode等から同一requestで複数回呼ばれるケースに対応
    mutable struct {
        Point origin = {0, 0};
        int16_t width = 0;
        DataRange range = {0, 0};
        bool valid = false;
    } dataRangeCache_;

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

    // getDataRangeキャッシュを無効化（アフィン行列が変わる可能性があるため）
    dataRangeCache_.valid = false;

    // Prepare時のoriginを保存（Process時の差分計算用）
    prepareOriginX_ = request.origin.x;
    prepareOriginY_ = request.origin.y;

    // 常に合成行列を計算し、アフィン事前計算を実行
    // request.affineMatrix経由の平行移動も含めて一貫した座標計算を行う
    AffineMatrix combinedMatrix;
    if (request.hasAffine) {
        combinedMatrix = request.affineMatrix * localMatrix_;
    } else {
        combinedMatrix = localMatrix_;  // 無変換時は単位行列
    }

    // 逆行列とピクセル中心オフセットを計算
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
        // copyQuadDDA対応フォーマットならバイリニア可能（出力はRGBA8_Straight）
        const bool useBilinear = (interpolationMode_ == InterpolationMode::Bilinear)
                               && source_.formatID
                               && source_.formatID->copyQuadDDA;

        if (useBilinear) {
            // バイリニア: 有効範囲はNearest同様 srcSize
            // 境界外ピクセルは copyQuadDDA の edgeFlags で透明として補間
            fpWidth_ = source_.width << INT_FIXED_SHIFT;
            fpHeight_ = source_.height << INT_FIXED_SHIFT;

            // バイリニア: edgeFadeFlagsに応じて各辺の範囲を拡張
            // フェード有効な辺のみ halfPixel 分拡張（フェードアウト領域用）
            // invA/invCの符号によって、どの辺がstart/endに対応するか変わる
            constexpr int32_t halfPixel = 1 << (INT_FIXED_SHIFT - 1);

            // X方向のフェード拡張
            int32_t hpAStart = 0, hpAEnd = 0;
            if (invA >= 0) {
                // 非反転: xs1_はLeft側、xs2_はRight側
                if (edgeFadeFlags_ & EdgeFade_Left) hpAStart = halfPixel;
                if (edgeFadeFlags_ & EdgeFade_Right) hpAEnd = halfPixel;
            } else {
                // 反転: xs1_はRight側、xs2_はLeft側
                if (edgeFadeFlags_ & EdgeFade_Right) hpAStart = -halfPixel;
                if (edgeFadeFlags_ & EdgeFade_Left) hpAEnd = -halfPixel;
            }

            // Y方向のフェード拡張
            int32_t hpCStart = 0, hpCEnd = 0;
            if (invC >= 0) {
                // 非反転: ys1_はTop側、ys2_はBottom側
                if (edgeFadeFlags_ & EdgeFade_Top) hpCStart = halfPixel;
                if (edgeFadeFlags_ & EdgeFade_Bottom) hpCEnd = halfPixel;
            } else {
                // 反転: ys1_はBottom側、ys2_はTop側
                if (edgeFadeFlags_ & EdgeFade_Bottom) hpCStart = -halfPixel;
                if (edgeFadeFlags_ & EdgeFade_Top) hpCEnd = -halfPixel;
            }

            xs1_ = invA + (invA < 0 ? fpWidth_ : -1) - hpAStart;
            xs2_ = invA + (invA < 0 ? 0 : (fpWidth_ - 1)) + hpAEnd;
            ys1_ = invC + (invC < 0 ? fpHeight_ : -1) - hpCStart;
            ys2_ = invC + (invC < 0 ? 0 : (fpHeight_ - 1)) + hpCEnd;

            useBilinear_ = true;
        } else {
            // 最近傍: pivot の小数部を保持
            fpWidth_ = source_.width << INT_FIXED_SHIFT;
            fpHeight_ = source_.height << INT_FIXED_SHIFT;

            xs1_ = invA + (invA < 0 ? fpWidth_ : -1);
            xs2_ = invA + (invA < 0 ? 0 : (fpWidth_ - 1));
            ys1_ = invC + (invC < 0 ? fpHeight_ : -1);
            ys2_ = invC + (invC < 0 ? 0 : (fpHeight_ - 1));

            useBilinear_ = false;
        }

        // baseTx/Ty は バイリニア・最近傍共通の計算式
        baseTxWithOffsets_ = affine_.invTxFixed
                           + srcPivotXFixed16
                           + affine_.rowOffsetX + affine_.dxOffsetX
                           + prepareOffsetX;
        baseTyWithOffsets_ = affine_.invTyFixed
                           + srcPivotYFixed16
                           + affine_.rowOffsetY + affine_.dxOffsetY
                           + prepareOffsetY;

        // DDA増分に基づく最適化判定
        // 等倍表示相当（逆行列2x2部分が単位行列）かつ最近傍の場合、
        // DDA をスキップし、高速な非アフィンパス（subView参照）を使用
        // バイリニア補間時はedgeFade等の処理にDDAが必要なためスキップしない
        constexpr int32_t one = 1 << INT_FIXED_SHIFT;
        bool isTranslationOnly =
            !useBilinear_ &&
            invA == one &&
            invD == one &&
            invB == 0 &&
            invC == 0;

        hasAffine_ = !isTranslationOnly;
    } else {
        // 逆行列が無効（特異行列）
        hasAffine_ = true;
    }

    // SourceNodeは終端なので上流への伝播なし
    // 出力側で必要なAABBを計算（常にcalcAffineAABBを使用）
    PrepareResponse result;
    result.status = PrepareStatus::Prepared;
    result.preferredFormat = source_.formatID;

    // バイリニア補間のフェード領域分を考慮した入力矩形
    // フェード有効な辺は0.5ピクセル拡張される
    float aabbWidth = static_cast<float>(source_.width);
    float aabbHeight = static_cast<float>(source_.height);
    int_fixed aabbPivotX = pivotX_;
    int_fixed aabbPivotY = pivotY_;
    if (useBilinear_) {
        constexpr float half = 0.5f;
        constexpr int_fixed halfFixed = 1 << (INT_FIXED_SHIFT - 1);
        if (edgeFadeFlags_ & EdgeFade_Left)   { aabbWidth += half; aabbPivotX += halfFixed; }
        if (edgeFadeFlags_ & EdgeFade_Right)   { aabbWidth += half; }
        if (edgeFadeFlags_ & EdgeFade_Top)    { aabbHeight += half; aabbPivotY += halfFixed; }
        if (edgeFadeFlags_ & EdgeFade_Bottom) { aabbHeight += half; }
    }

    calcAffineAABB(
        aabbWidth, aabbHeight,
        {aabbPivotX, aabbPivotY},
        combinedMatrix,
        result.width, result.height, result.origin);

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

    // アフィン事前計算値から座標を導出（DDAパスと同一の情報源）
    // baseTxWithOffsets_ はPrepare時に合成行列から計算済みで、
    // request.affineMatrix経由の平行移動も含まれている
    const int32_t deltaX = from_fixed(request.origin.x - prepareOriginX_);
    const int32_t deltaY = from_fixed(request.origin.y - prepareOriginY_);
    const int32_t baseX = baseTxWithOffsets_ + deltaX * affine_.invMatrix.a + deltaY * affine_.invMatrix.b;
    const int32_t baseY = baseTyWithOffsets_ + deltaX * affine_.invMatrix.c + deltaY * affine_.invMatrix.d;

    // srcBase: 出力dx=0に対応するソースピクセルインデックス
    int32_t srcBaseX = from_fixed_floor(baseX);
    int32_t srcBaseY = from_fixed_floor(baseY);

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
    // カラーキー情報を出力ImageBufferに設定
    if (colorKeyRGBA8_ != colorKeyReplace_) {
        result.auxInfo().colorKeyRGBA8 = colorKeyRGBA8_;
        result.auxInfo().colorKeyReplace = colorKeyReplace_;
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

// getDataRange: スキャンライン単位の正確なデータ範囲を返す
// アフィン変換時はcalcScanlineRangeで厳密な有効範囲を計算
// 同一リクエストの重複呼び出しはキャッシュで高速化
DataRange SourceNode::getDataRange(const RenderRequest& request) const {
    // アフィン変換がない場合はAABBベースで十分（正確）
    if (!hasAffine_) {
        return prepareResponse_.getDataRange(request);
    }

    // キャッシュヒットチェック（同一スキャンラインの重複呼び出し対応）
    if (dataRangeCache_.valid
        && dataRangeCache_.origin.x == request.origin.x
        && dataRangeCache_.origin.y == request.origin.y
        && dataRangeCache_.width == request.width) {
        return dataRangeCache_.range;
    }

    // calcScanlineRangeで正確な有効範囲を計算
    int32_t dxStart = 0, dxEnd = 0;
    DataRange result;
    if (calcScanlineRange(request, dxStart, dxEnd, nullptr, nullptr)) {
        result = DataRange{static_cast<int16_t>(dxStart),
                           static_cast<int16_t>(dxEnd + 1)};
    } else {
        result = DataRange{0, 0};
    }

    // キャッシュ更新
    dataRangeCache_.origin = request.origin;
    dataRangeCache_.width = request.width;
    dataRangeCache_.range = result;
    dataRangeCache_.valid = true;

    return result;
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

    // 空のResponseを取得し、バッファを直接作成（ムーブなし）
    int validWidth = dxEnd - dxStart + 1;
    RenderResponse& resp = makeEmptyResponse(adjustedOrigin);
    // バイリニア補間時はRGBA8_Straight出力、それ以外はソースフォーマット
    PixelFormatID outFormat = useBilinear_ ? PixelFormatIDs::RGBA8_Straight : source_.formatID;
    ImageBuffer* output = resp.createBuffer(
        validWidth, 1, outFormat, InitPolicy::Uninitialized);

    if (!output) {
        return resp;  // バッファ作成失敗時は空のResponseを返す
    }

    // バッファにワールド座標originを設定（makeResponseを使わないパス）
    output->setOrigin(adjustedOrigin);

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
        // バイリニア補間（出力はRGBA8_Straight）
        // 0.5ピクセル減算（ピクセル中心→左上基準への変換）
        constexpr int32_t halfPixel = 1 << (INT_FIXED_SHIFT - 1);
        // パレット情報をPixelAuxInfoとして渡す（Index8のパレット展開用）
        PixelAuxInfo auxInfo;
        if (palette_) {
            auxInfo.palette = palette_.data;
            auxInfo.paletteFormat = palette_.format;
            auxInfo.paletteColorCount = palette_.colorCount;
        }
        if (colorKeyRGBA8_ != colorKeyReplace_) {
            auxInfo.colorKeyRGBA8 = colorKeyRGBA8_;
            auxInfo.colorKeyReplace = colorKeyReplace_;
        }
        const PixelAuxInfo* auxPtr = (auxInfo.palette || auxInfo.colorKeyRGBA8 != auxInfo.colorKeyReplace)
                                   ? &auxInfo : nullptr;
        view_ops::copyRowDDABilinear(dstRow, source_, validWidth,
            srcX_fixed - halfPixel, srcY_fixed - halfPixel, invA, invC, edgeFadeFlags_, auxPtr);
    } else {
        // 最近傍補間（BPP分岐は関数内部で実施）
        // view_ops::copyRowDDA(dstRow, source_, validWidth,
        //     srcX_fixed, srcY_fixed, invA, invC);

        // DDAParam を構築（srcWidth/srcHeight/weightsはcopyRowDDAでは使用しない）
        DDAParam param = { source_.stride, 0, 0, srcX_fixed, srcY_fixed, invA, invC, nullptr, nullptr };

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
    // カラーキー情報を出力ImageBufferに設定
    if (colorKeyRGBA8_ != colorKeyReplace_) {
        output->auxInfo().colorKeyRGBA8 = colorKeyRGBA8_;
        output->auxInfo().colorKeyReplace = colorKeyReplace_;
    }

    return resp;
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_SOURCE_NODE_H
