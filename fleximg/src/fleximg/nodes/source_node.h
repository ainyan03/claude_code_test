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

class SourceNode : public Node {
public:
    // コンストラクタ
    SourceNode() {
        initPorts(0, 1);  // 入力0、出力1
    }

    SourceNode(const ViewPort& vp, int_fixed originX = 0, int_fixed originY = 0)
        : source_(vp), originX_(originX), originY_(originY) {
        initPorts(0, 1);
    }

    // ソース設定
    void setSource(const ViewPort& vp) { source_ = vp; }
    void setOrigin(int_fixed x, int_fixed y) { originX_ = x; originY_ = y; }

    // アクセサ
    const ViewPort& source() const { return source_; }
    int_fixed originX() const { return originX_; }
    int_fixed originY() const { return originY_; }

    // ユーザー向けAPI: pivot（内部 origin のラッパー）
    void setPivot(float x, float y) {
        originX_ = float_to_fixed(x);
        originY_ = float_to_fixed(y);
    }
    std::pair<float, float> getPivot() const {
        return {fixed_to_float(originX_), fixed_to_float(originY_)};
    }

    // ユーザー向けAPI: position（配置位置）
    void setPosition(float x, float y) {
        positionX_ = x;
        positionY_ = y;
    }
    std::pair<float, float> getPosition() const {
        return {positionX_, positionY_};
    }

    // 補間モード設定
    void setInterpolationMode(InterpolationMode mode) { interpolationMode_ = mode; }
    InterpolationMode interpolationMode() const { return interpolationMode_; }

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
        // position が設定されている場合も、アフィン行列に合成して処理
        if (request.hasAffine || positionX_ != 0.0f || positionY_ != 0.0f) {
            // アフィン行列がない場合は単位行列を使用し、position を tx/ty として設定
            AffineMatrix mat = request.hasAffine
                ? request.affineMatrix
                : AffineMatrix{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
            // position をアフィン行列の 2x2 部分で変換してから加算
            // これにより、各ソースの位置関係がアフィン変換後も維持される
            float transformedPosX = mat.a * positionX_ + mat.b * positionY_;
            float transformedPosY = mat.c * positionX_ + mat.d * positionY_;
            mat.tx += transformedPosX;
            mat.ty += transformedPosY;

            // 逆行列とピクセル中心オフセットを計算（共通処理）
            affine_ = precomputeInverseAffine(mat);

            if (affine_.isValid()) {
                const int32_t invA = affine_.invMatrix.a;
                const int32_t invC = affine_.invMatrix.c;

                // origin は既に Q16.16 なのでそのまま使用
                const int32_t srcOriginXFixed16 = originX_;
                const int32_t srcOriginYFixed16 = originY_;

                // バイリニア補間かどうかで有効範囲とオフセットが異なる
                const bool useBilinear = (interpolationMode_ == InterpolationMode::Bilinear)
                                       && (source_.formatID == PixelFormatIDs::RGBA8_Straight);

                if (useBilinear) {
                    // バイリニア: 有効範囲は srcSize - 1 + ε
                    // +1 により端ピクセル（小数部=0）も有効範囲に含める
                    // 端での隣接ピクセルアクセスは copyRowDDABilinear_RGBA8888 側でクランプ
                    fpWidth_ = ((source_.width - 1) << INT_FIXED16_SHIFT) + 1;
                    fpHeight_ = ((source_.height - 1) << INT_FIXED16_SHIFT) + 1;

                    xs1_ = invA + (invA < 0 ? fpWidth_ : -1);
                    xs2_ = invA + (invA < 0 ? 0 : (fpWidth_ - 1));
                    ys1_ = invC + (invC < 0 ? fpHeight_ : -1);
                    ys2_ = invC + (invC < 0 ? 0 : (fpHeight_ - 1));

                    // バイリニア: origin の小数部を保持し、0.5 ピクセル減算（ピクセル中心基準）
                    constexpr int32_t halfPixel = 1 << (INT_FIXED16_SHIFT - 1);  // 0.5 in Q16.16
                    baseTxWithOffsets_ = affine_.invTxFixed
                                       + srcOriginXFixed16 - halfPixel
                                       + affine_.rowOffsetX + affine_.dxOffsetX;
                    baseTyWithOffsets_ = affine_.invTyFixed
                                       + srcOriginYFixed16 - halfPixel
                                       + affine_.rowOffsetY + affine_.dxOffsetY;
                    useBilinear_ = true;
                } else {
                    // 最近傍: origin の小数部を保持
                    fpWidth_ = source_.width << INT_FIXED16_SHIFT;
                    fpHeight_ = source_.height << INT_FIXED16_SHIFT;

                    xs1_ = invA + (invA < 0 ? fpWidth_ : -1);
                    xs2_ = invA + (invA < 0 ? 0 : (fpWidth_ - 1));
                    ys1_ = invC + (invC < 0 ? fpHeight_ : -1);
                    ys2_ = invC + (invC < 0 ? 0 : (fpHeight_ - 1));

                    // dxOffset を含める（半ピクセルオフセット）
                    baseTxWithOffsets_ = affine_.invTxFixed
                                       + srcOriginXFixed16
                                       + affine_.rowOffsetX + affine_.dxOffsetX;
                    baseTyWithOffsets_ = affine_.invTyFixed
                                       + srcOriginYFixed16
                                       + affine_.rowOffsetY + affine_.dxOffsetY;
                    useBilinear_ = false;
                }

                // ドットバイドット判定（逆行列の増分値で判定）
                // 単位行列（変換なし）の場合のみ DDA をスキップし、高速な非アフィンパスを使用
                // origin の小数部もチェック（アフィンパスでは整数部のみ使用するため）
                // 注: invTxFixed/invTyFixed がゼロでない場合は、アフィン行列に平行移動が
                // 含まれているため、非アフィンパスは使えない（NinePatch のパッチ位置など）
                // 注: a == -one や d == -one（反転）の場合も非アフィンパスでは処理できない
                constexpr int32_t one = 1 << INT_FIXED16_SHIFT;  // 65536 (Q16.16)
                bool isDotByDot =
                    affine_.invMatrix.a == one &&          // a == 1 のみ（-1は反転なので不可）
                    affine_.invMatrix.d == one &&          // d == 1 のみ（-1は反転なので不可）
                    affine_.invMatrix.b == 0 &&
                    affine_.invMatrix.c == 0 &&
                    affine_.invTxFixed == 0 &&             // 平行移動がない（tx = 0）
                    affine_.invTyFixed == 0 &&             // 平行移動がない（ty = 0）
                    (originX_ & 0xFFFF) == 0 &&            // origin の小数部が0（Q16.16）
                    (originY_ & 0xFFFF) == 0;

                if (isDotByDot) {
                    hasAffine_ = false;
                    // position は pullProcess の非アフィンパスで origin オフセットとして適用
                } else {
                    hasAffine_ = true;
                }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
                printf("[SourceNode] isDotByDot=%s (invA=%d, invD=%d, invB=%d, invC=%d, txFrac=%d, tyFrac=%d, oxFrac=%d, oyFrac=%d)\n",
                       isDotByDot ? "true" : "false",
                       affine_.invMatrix.a, affine_.invMatrix.d,
                       affine_.invMatrix.b, affine_.invMatrix.c,
                       affine_.invTxFixed & 0xFFFF, affine_.invTyFixed & 0xFFFF,
                       originX_ & 0xFFFF, originY_ & 0xFFFF);
#endif
            } else {
                // 逆行列が無効（特異行列）
                hasAffine_ = true;
            }
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

        // ソース画像の基準相対座標範囲（固定小数点 Q16.16）
        // position が設定されている場合、origin からのオフセットとして適用
        int_fixed posOffsetX = float_to_fixed(positionX_);
        int_fixed posOffsetY = float_to_fixed(positionY_);
        int_fixed imgLeft = -originX_ + posOffsetX;
        int_fixed imgTop = -originY_ + posOffsetY;
        int_fixed imgRight = imgLeft + to_fixed(source_.width);
        int_fixed imgBottom = imgTop + to_fixed(source_.height);

        // 要求範囲の基準相対座標（固定小数点 Q16.16）
        int_fixed reqLeft = -request.origin.x;
        int_fixed reqTop = -request.origin.y;
        int_fixed reqRight = reqLeft + to_fixed(request.width);
        int_fixed reqBottom = reqTop + to_fixed(request.height);

        // 交差領域
        int_fixed interLeft = std::max(imgLeft, reqLeft);
        int_fixed interTop = std::max(imgTop, reqTop);
        int_fixed interRight = std::min(imgRight, reqRight);
        int_fixed interBottom = std::min(imgBottom, reqBottom);

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
        int srcX = from_fixed_floor(interLeft - imgLeft);
        int srcY = from_fixed_floor(interTop - imgTop);
        int srcEndX = from_fixed_ceil(interRight - imgLeft);
        int srcEndY = from_fixed_ceil(interBottom - imgTop);
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
    int_fixed originX_ = 0;  // 画像内の基準点X（固定小数点 Q16.16）
    int_fixed originY_ = 0;  // 画像内の基準点Y（固定小数点 Q16.16）
    float positionX_ = 0.0f;  // 配置位置X（アフィン行列の tx に合成）
    float positionY_ = 0.0f;  // 配置位置Y（アフィン行列の ty に合成）
    InterpolationMode interpolationMode_ = InterpolationMode::Nearest;

    // アフィン伝播用メンバ変数（事前計算済み）
    AffinePrecomputed affine_;     // 逆行列・ピクセル中心オフセット
    bool hasAffine_ = false;       // アフィン変換が伝播されているか
    bool useBilinear_ = false;     // バイリニア補間を使用するか（事前計算結果）

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

        // dstOrigin分を計算（Q16.16 → int）
        const int32_t dstOriginXInt = from_fixed(request.origin.x);
        const int32_t dstOriginYInt = from_fixed(request.origin.y);

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
        // srcX_fixed = invA * dxStart + baseXWithHalf
        // バイリニア時: baseXWithHalfにdxOffsetXは含まれていない（小数部を補間重みとして使用）
        // 最近傍時: baseXWithHalfにdxOffsetXは含まれている
        int32_t srcX_fixed = invA * dxStart + baseXWithHalf;
        int32_t srcY_fixed = invC * dxStart + baseYWithHalf;

        uint8_t* dstRow = static_cast<uint8_t*>(output.data());
        const uint8_t* srcData = static_cast<const uint8_t*>(source_.data);

        if (useBilinear_) {
            // バイリニア補間（RGBA8888専用）
            transform::copyRowDDABilinear_RGBA8888(dstRow, source_,
                srcX_fixed, srcY_fixed, invA, invC, validWidth);
        } else {
            // 最近傍補間
            switch (getBytesPerPixel(source_.formatID)) {
                case 8:
                    transform::copyRowDDA<8>(dstRow, srcData, source_.stride,
                        srcX_fixed, srcY_fixed, invA, invC, validWidth);
                    break;
                case 4:
                    transform::copyRowDDA<4>(dstRow, srcData, source_.stride,
                        srcX_fixed, srcY_fixed, invA, invC, validWidth);
                    break;
                case 3:
                    transform::copyRowDDA<3>(dstRow, srcData, source_.stride,
                        srcX_fixed, srcY_fixed, invA, invC, validWidth);
                    break;
                case 2:
                    transform::copyRowDDA<2>(dstRow, srcData, source_.stride,
                        srcX_fixed, srcY_fixed, invA, invC, validWidth);
                    break;
                case 1:
                    transform::copyRowDDA<1>(dstRow, srcData, source_.stride,
                        srcX_fixed, srcY_fixed, invA, invC, validWidth);
                    break;
                default:
                    break;
            }
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
            request.origin.x - to_fixed(dxStart),
            request.origin.y
        };

        return RenderResult(std::move(output), adjustedOrigin);
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_SOURCE_NODE_H
