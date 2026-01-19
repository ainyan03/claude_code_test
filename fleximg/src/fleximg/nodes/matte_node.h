#ifndef FLEXIMG_MATTE_NODE_H
#define FLEXIMG_MATTE_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../image/pixel_format.h"
#include "../operations/canvas_utils.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// MatteNode - マット合成ノード
// ========================================================================
//
// 3つの入力画像を使ってマット合成（アルファマスク合成）を行います。
// - 入力ポート0: 元画像1（前景、マスク白部分に表示）
// - 入力ポート1: 元画像2（背景、マスク黒部分に表示）
// - 入力ポート2: アルファマスク（Alpha8推奨）
// - 出力: 1ポート
//
// 計算式:
//   Output = Image1 × Alpha + Image2 × (1 - Alpha)
//
// 未接続・範囲外の扱い:
// - 元画像1/2: 透明の黒 (0,0,0,0)
// - アルファマスク: alpha=0（全面背景）
//
// 使用例:
//   MatteNode matte;
//   foreground >> matte;              // ポート0（前景）
//   background.connectTo(matte, 1);   // ポート1（背景）
//   mask.connectTo(matte, 2);         // ポート2（マスク）
//   matte >> sink;
//

class MatteNode : public Node {
public:
    MatteNode() {
        initPorts(3, 1);  // 3入力、1出力
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "MatteNode"; }

protected:
    int nodeTypeForMetrics() const override { return NodeType::Matte; }

public:
    // ========================================
    // プル型インターフェース
    // ========================================

    // 全上流ノードにPrepareRequestを伝播
    bool pullPrepare(const PrepareRequest& request) override {
        bool shouldContinue;
        if (!checkPrepareState(pullPrepareState_, shouldContinue)) {
            return false;
        }
        if (!shouldContinue) {
            return true;  // DAG共有ノード: スキップ
        }

        // 全上流へ伝播（3入力）
        for (int i = 0; i < 3; ++i) {
            Node* upstream = upstreamNode(i);
            if (upstream) {
                if (!upstream->pullPrepare(request)) {
                    pullPrepareState_ = PrepareState::CycleError;
                    return false;
                }
            }
        }

        // 準備処理
        RenderRequest screenInfo;
        screenInfo.width = request.width;
        screenInfo.height = request.height;
        screenInfo.origin = request.origin;
        prepare(screenInfo);

        pullPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // 全上流ノードに終了を伝播
    void pullFinalize() override {
        if (pullPrepareState_ == PrepareState::Idle) {
            return;
        }
        pullPrepareState_ = PrepareState::Idle;

        finalize();
        for (int i = 0; i < 3; ++i) {
            Node* upstream = upstreamNode(i);
            if (upstream) {
                upstream->pullFinalize();
            }
        }
    }

    // マット合成処理
    RenderResult pullProcess(const RenderRequest& request) override {
        if (pullPrepareState_ != PrepareState::Prepared) {
            return RenderResult();
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto startTime = std::chrono::high_resolution_clock::now();
#endif

        // 各入力から画像を取得
        RenderResult img1Result, img2Result, maskResult;

        Node* upstream0 = upstreamNode(0);  // 前景
        Node* upstream1 = upstreamNode(1);  // 背景
        Node* upstream2 = upstreamNode(2);  // マスク

        if (upstream0) {
            img1Result = upstream0->pullProcess(request);
        }
        if (upstream1) {
            img2Result = upstream1->pullProcess(request);
        }
        if (upstream2) {
            maskResult = upstream2->pullProcess(request);
        }

        // すべて空なら空を返す
        if (!img1Result.isValid() && !img2Result.isValid() && !maskResult.isValid()) {
            return RenderResult();
        }

        // 出力バッファを確保（RGBA8_Straightで作成）
        ImageBuffer outputBuf(request.width, request.height, PixelFormatIDs::RGBA8_Straight);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Matte].recordAlloc(
            outputBuf.totalBytes(), outputBuf.width(), outputBuf.height());
#endif

        // フォーマット変換（RGBA8_StraightまたはAlpha8）
        // 元画像はRGBA8_Straightに変換
        if (img1Result.isValid()) {
            img1Result.buffer = convertFormat(std::move(img1Result.buffer),
                                              PixelFormatIDs::RGBA8_Straight);
        }
        if (img2Result.isValid()) {
            img2Result.buffer = convertFormat(std::move(img2Result.buffer),
                                              PixelFormatIDs::RGBA8_Straight);
        }
        // マスクはAlpha8に変換
        if (maskResult.isValid()) {
            maskResult.buffer = convertFormat(std::move(maskResult.buffer),
                                              PixelFormatIDs::Alpha8);
        }

        // マット合成処理
        applyMatteComposite(outputBuf, request,
                           img1Result, img2Result, maskResult);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Matte];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - startTime).count();
        metrics.count++;
#endif

        return RenderResult(std::move(outputBuf), request.origin);
    }

private:
    // マット合成の実処理
    void applyMatteComposite(ImageBuffer& output, const RenderRequest& request,
                             const RenderResult& img1, const RenderResult& img2,
                             const RenderResult& mask) {
        ViewPort outView = output.view();
        uint8_t* outPtr = static_cast<uint8_t*>(outView.data);
        int outStride = outView.stride;

        // 出力の原点（固定小数点）
        int_fixed outOriginX = request.origin.x;
        int_fixed outOriginY = request.origin.y;

        // 各入力のビュー情報（nullチェック付き）
        const uint8_t* img1Ptr = nullptr;
        const uint8_t* img2Ptr = nullptr;
        const uint8_t* maskPtr = nullptr;
        int img1Width = 0, img1Height = 0, img1Stride = 0;
        int img2Width = 0, img2Height = 0, img2Stride = 0;
        int maskWidth = 0, maskHeight = 0, maskStride = 0;
        int_fixed img1OriginX = 0, img1OriginY = 0;
        int_fixed img2OriginX = 0, img2OriginY = 0;
        int_fixed maskOriginX = 0, maskOriginY = 0;

        if (img1.isValid()) {
            ViewPort v = img1.view();
            img1Ptr = static_cast<const uint8_t*>(v.data);
            img1Width = v.width;
            img1Height = v.height;
            img1Stride = v.stride;
            img1OriginX = img1.origin.x;
            img1OriginY = img1.origin.y;
        }
        if (img2.isValid()) {
            ViewPort v = img2.view();
            img2Ptr = static_cast<const uint8_t*>(v.data);
            img2Width = v.width;
            img2Height = v.height;
            img2Stride = v.stride;
            img2OriginX = img2.origin.x;
            img2OriginY = img2.origin.y;
        }
        if (mask.isValid()) {
            ViewPort v = mask.view();
            maskPtr = static_cast<const uint8_t*>(v.data);
            maskWidth = v.width;
            maskHeight = v.height;
            maskStride = v.stride;
            maskOriginX = mask.origin.x;
            maskOriginY = mask.origin.y;
        }

        // ピクセル単位で合成
        // 座標変換: 出力ピクセル(x,y) → 世界座標 → 入力ピクセル
        // 世界座標 = 出力ピクセル - 出力origin
        // 入力ピクセル = 世界座標 + 入力origin
        for (int y = 0; y < request.height; ++y) {
            for (int x = 0; x < request.width; ++x) {
                // 各入力からピクセル値を取得
                uint8_t r1 = 0, g1 = 0, b1 = 0, a1 = 0;  // 前景（デフォルト透明黒）
                uint8_t r2 = 0, g2 = 0, b2 = 0, a2 = 0;  // 背景（デフォルト透明黒）
                uint8_t alpha = 0;                        // マスク（デフォルト0=全面背景）

                // 前景から取得（RGBA8_Straight: 4バイト/ピクセル）
                if (img1Ptr) {
                    // 入力座標 = (出力ピクセル - 出力origin) + 入力origin
                    int sx = from_fixed(to_fixed(x) - outOriginX + img1OriginX);
                    int sy = from_fixed(to_fixed(y) - outOriginY + img1OriginY);
                    if (sx >= 0 && sx < img1Width && sy >= 0 && sy < img1Height) {
                        const uint8_t* p = img1Ptr + sy * img1Stride + sx * 4;
                        r1 = p[0];
                        g1 = p[1];
                        b1 = p[2];
                        a1 = p[3];
                    }
                }

                // 背景から取得（RGBA8_Straight: 4バイト/ピクセル）
                if (img2Ptr) {
                    int sx = from_fixed(to_fixed(x) - outOriginX + img2OriginX);
                    int sy = from_fixed(to_fixed(y) - outOriginY + img2OriginY);
                    if (sx >= 0 && sx < img2Width && sy >= 0 && sy < img2Height) {
                        const uint8_t* p = img2Ptr + sy * img2Stride + sx * 4;
                        r2 = p[0];
                        g2 = p[1];
                        b2 = p[2];
                        a2 = p[3];
                    }
                }

                // マスクから取得（Alpha8: 1バイト/ピクセル）
                if (maskPtr) {
                    int sx = from_fixed(to_fixed(x) - outOriginX + maskOriginX);
                    int sy = from_fixed(to_fixed(y) - outOriginY + maskOriginY);
                    if (sx >= 0 && sx < maskWidth && sy >= 0 && sy < maskHeight) {
                        alpha = maskPtr[sy * maskStride + sx];
                    }
                }

                // マット合成: Out = Img1 * alpha + Img2 * (1 - alpha)
                uint8_t inv_alpha = 255 - alpha;
                uint8_t* outP = outPtr + y * outStride + x * 4;

                outP[0] = static_cast<uint8_t>((r1 * alpha + r2 * inv_alpha) / 255);
                outP[1] = static_cast<uint8_t>((g1 * alpha + g2 * inv_alpha) / 255);
                outP[2] = static_cast<uint8_t>((b1 * alpha + b2 * inv_alpha) / 255);
                outP[3] = static_cast<uint8_t>((a1 * alpha + a2 * inv_alpha) / 255);
            }
        }
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_MATTE_NODE_H
