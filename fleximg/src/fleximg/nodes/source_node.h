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
    void setSource(const ViewPort& vp) { source_ = vp; }

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
    RenderResponse onPullProcess(const RenderRequest& request) override;

    // getDataRange: アフィン変換を考慮した正確なデータ範囲を返す
    // アフィン変換がある場合、AABB ではなくスキャンラインごとの正確な範囲を計算
    DataRange getDataRange(const RenderRequest& request) const override;

private:
    ViewPort source_;
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

    // getDataRange/pullProcess 間のキャッシュ
    // originが極端な値（INT32_MIN）の場合はキャッシュ無効
    struct DataRangeCache {
        Point origin = {INT32_MIN, INT32_MIN};  // キャッシュキー（無効値で初期化）
        int32_t dxStart = 0;
        int32_t dxEnd = 0;
        int32_t baseXWithHalf = 0;  // DDA用ベース座標X
        int32_t baseYWithHalf = 0;  // DDA用ベース座標Y
    };
    mutable DataRangeCache rangeCache_;

    // キャッシュを無効化
    void invalidateRangeCache() { rangeCache_.origin = {INT32_MIN, INT32_MIN}; }

    // スキャンライン有効範囲を計算（getDataRange/pullProcessWithAffineで共用）
    // 戻り値: true=有効範囲あり, false=有効範囲なし
    // baseXWithHalf/baseYWithHalf はオプショナル出力（nullptrなら出力しない）
    bool calcScanlineRange(const RenderRequest& request,
                           int32_t& dxStart, int32_t& dxEnd,
                           int32_t* baseXWithHalf = nullptr,
                           int32_t* baseYWithHalf = nullptr) const;

    // アフィン変換付きプル処理（スキャンライン専用）
    RenderResponse pullProcessWithAffine(const RenderRequest& request);
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

    // 範囲キャッシュを無効化（アフィン行列が変わる可能性があるため）
    invalidateRangeCache();

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

            // ドットバイドット判定（逆行列の増分値で判定）
            // 単位行列（変換なし）の場合のみ DDA をスキップし、高速な非アフィンパスを使用
            // pivot の小数部もチェック（アフィンパスでは整数部のみ使用するため）
            // 注: invTxFixed/invTyFixed がゼロでない場合は、アフィン行列に平行移動が
            // 含まれているため、非アフィンパスは使えない（NinePatch のパッチ位置など）
            // 注: a == -one や d == -one（反転）の場合も非アフィンパスでは処理できない
            constexpr int32_t one = 1 << INT_FIXED_SHIFT;  // 65536 (Q16.16)
            bool isDotByDot =
                affine_.invMatrix.a == one &&          // a == 1 のみ（-1は反転なので不可）
                affine_.invMatrix.d == one &&          // d == 1 のみ（-1は反転なので不可）
                affine_.invMatrix.b == 0 &&
                affine_.invMatrix.c == 0 &&
                affine_.invTxFixed == 0 &&             // 平行移動がない（tx = 0）
                affine_.invTyFixed == 0 &&             // 平行移動がない（ty = 0）
                (pivotX_ & 0xFFFF) == 0 &&             // pivot の小数部が0（Q16.16）
                (pivotY_ & 0xFFFF) == 0;

            if (isDotByDot) {
                hasAffine_ = false;
                // position は pullProcess の非アフィンパスで pivot オフセットとして適用
            } else {
                hasAffine_ = true;
            }

// Note: 詳細デバッグ出力は実機では負荷が高いためコメントアウト
// #ifdef FLEXIMG_DEBUG_PERF_METRICS
//             printf("[SourceNode] isDotByDot=%s (invA=%d, invD=%d, invB=%d, invC=%d, txFrac=%d, tyFrac=%d, pxFrac=%d, pyFrac=%d)\n",
//                    isDotByDot ? "true" : "false",
//                    affine_.invMatrix.a, affine_.invMatrix.d,
//                    affine_.invMatrix.b, affine_.invMatrix.c,
//                    affine_.invTxFixed & 0xFFFF, affine_.invTyFixed & 0xFFFF,
//                    pivotX_ & 0xFFFF, pivotY_ & 0xFFFF);
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

RenderResponse SourceNode::onPullProcess(const RenderRequest& request) {
    FLEXIMG_METRICS_SCOPE(NodeType::Source);

    if (!source_.isValid()) {
        return RenderResponse();
    }

    // アフィン変換が伝播されている場合はDDA処理
    if (hasAffine_) {
        return pullProcessWithAffine(request);
    }

    // ソース画像のワールド座標範囲（固定小数点 Q16.16）
    // localMatrix_.tx/ty が設定されている場合、position として適用
    int_fixed posOffsetX = float_to_fixed(localMatrix_.tx);
    int_fixed posOffsetY = float_to_fixed(localMatrix_.ty);
    int_fixed imgLeft = posOffsetX - pivotX_;   // 画像左端のワールドX座標
    int_fixed imgTop = posOffsetY - pivotY_;    // 画像上端のワールドY座標
    int_fixed imgRight = imgLeft + to_fixed(source_.width);
    int_fixed imgBottom = imgTop + to_fixed(source_.height);

    // 要求範囲のワールド座標（固定小数点 Q16.16）
    int_fixed reqLeft = request.origin.x;
    int_fixed reqTop = request.origin.y;
    int_fixed reqRight = reqLeft + to_fixed(request.width);
    int_fixed reqBottom = reqTop + to_fixed(request.height);

    // 交差領域
    int_fixed interLeft = std::max(imgLeft, reqLeft);
    int_fixed interTop = std::max(imgTop, reqTop);
    int_fixed interRight = std::min(imgRight, reqRight);
    int_fixed interBottom = std::min(imgBottom, reqBottom);

    if (interLeft >= interRight || interTop >= interBottom) {
        // バッファ内基準点位置 = request.origin
        return RenderResponse(ImageBuffer(), request.origin);
    }

    // 交差領域のサブビューを参照モードで返す（コピーなし）
    // 開始位置は floor、終端位置は ceil で計算（タイル境界でのピクセル欠落を防ぐ）
    auto srcX = static_cast<int_fast16_t>(from_fixed_floor(interLeft - imgLeft));
    auto srcY = static_cast<int_fast16_t>(from_fixed_floor(interTop - imgTop));
    auto srcEndX = static_cast<int_fast16_t>(from_fixed_ceil(interRight - imgLeft));
    auto srcEndY = static_cast<int_fast16_t>(from_fixed_ceil(interBottom - imgTop));
    auto interW = static_cast<int_fast16_t>(srcEndX - srcX);
    auto interH = static_cast<int_fast16_t>(srcEndY - srcY);

    // サブビューの参照モードImageBufferを作成（メモリ確保なし）
    ImageBuffer result(view_ops::subView(source_, srcX, srcY, interW, interH));

    // origin = 交差領域左上のワールド座標
    return RenderResponse(std::move(result), Point{interLeft, interTop});
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

// getDataRange: アフィン変換を考慮した正確なデータ範囲を返す
DataRange SourceNode::getDataRange(const RenderRequest& request) const {
    // アフィン変換がない場合は基底クラスの実装を使用
    if (!hasAffine_) {
        return Node::getDataRange(request);
    }

    // スキャンライン範囲を計算（baseX/baseY もキャッシュ用に取得）
    int32_t dxStart, dxEnd, baseX, baseY;
    bool hasData = calcScanlineRange(request, dxStart, dxEnd, &baseX, &baseY);

    // キャッシュに保存（pullProcessWithAffine で再利用）
    rangeCache_.origin = request.origin;
    rangeCache_.dxStart = dxStart;
    rangeCache_.dxEnd = dxEnd;
    rangeCache_.baseXWithHalf = baseX;
    rangeCache_.baseYWithHalf = baseY;

    if (!hasData) {
        return DataRange{0, 0};  // 有効範囲なし
    }

    return DataRange{static_cast<int16_t>(dxStart), static_cast<int16_t>(dxEnd + 1)};  // endXは排他的
}

// アフィン変換付きプル処理（スキャンライン専用）
// 前提: request.height == 1（RendererNodeはスキャンライン単位で処理）
// 有効範囲のみのバッファを返し、範囲外の0データを下流に送らない
RenderResponse SourceNode::pullProcessWithAffine(const RenderRequest& request) {
    int32_t dxStart, dxEnd, baseX, baseY;

    // キャッシュが有効か確認
    if (rangeCache_.origin.x == request.origin.x &&
        rangeCache_.origin.y == request.origin.y) {
        // キャッシュヒット: 全ての値をキャッシュから取得
        dxStart = rangeCache_.dxStart;
        dxEnd = rangeCache_.dxEnd;
        baseX = rangeCache_.baseXWithHalf;
        baseY = rangeCache_.baseYWithHalf;
    } else {
        // キャッシュミス: 計算が必要
        if (!calcScanlineRange(request, dxStart, dxEnd, &baseX, &baseY)) {
            return RenderResponse(ImageBuffer(), request.origin);
        }
    }

    // 有効ピクセルがない場合
    if (dxStart > dxEnd) {
        return RenderResponse(ImageBuffer(), request.origin);
    }

    // 有効範囲のみのバッファを作成
    int validWidth = dxEnd - dxStart + 1;
    ImageBuffer output(validWidth, 1, source_.formatID, InitPolicy::Uninitialized, allocator_);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().nodes[NodeType::Source].recordAlloc(
        output.totalBytes(), output.width(), output.height());
#endif

    // DDA転写（1行のみ）
    const int32_t invA = affine_.invMatrix.a;
    const int32_t invC = affine_.invMatrix.c;
    int32_t srcX_fixed = invA * dxStart + baseX;
    int32_t srcY_fixed = invC * dxStart + baseY;

    void* dstRow = output.data();

    if (useBilinear_) {
        // バイリニア補間（RGBA8888専用、他フォーマットは最近傍フォールバック）
        view_ops::copyRowDDABilinear(dstRow, source_, validWidth,
            srcX_fixed, srcY_fixed, invA, invC);
    } else {
        // 最近傍補間（BPP分岐は関数内部で実施）
        view_ops::copyRowDDA(dstRow, source_, validWidth,
            srcX_fixed, srcY_fixed, invA, invC);
    }

    // originを有効範囲に合わせて調整
    // dxStart分だけ右にオフセット（バッファ左端のワールド座標）
    Point adjustedOrigin = {
        request.origin.x + to_fixed(dxStart),
        request.origin.y
    };

    return RenderResponse(std::move(output), adjustedOrigin);
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_SOURCE_NODE_H
