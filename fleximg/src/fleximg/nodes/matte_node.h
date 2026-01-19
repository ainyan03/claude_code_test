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

        Node* upstream0 = upstreamNode(0);  // 前景
        Node* upstream1 = upstreamNode(1);  // 背景
        Node* upstream2 = upstreamNode(2);  // マスク

        // ========================================
        // Step 1: 元画像1と元画像2を先に評価
        // ========================================
        RenderResult img1Result, img2Result;

        if (upstream0) {
            img1Result = upstream0->pullProcess(request);
        }
        if (upstream1) {
            img2Result = upstream1->pullProcess(request);
        }

        // 両方空ならマスクも評価せず空を返す
        if (!img1Result.isValid() && !img2Result.isValid()) {
            return RenderResult();
        }

        // フォーマット変換（RGBA8_Straight）
        if (img1Result.isValid()) {
            img1Result.buffer = convertFormat(std::move(img1Result.buffer),
                                              PixelFormatIDs::RGBA8_Straight);
        }
        if (img2Result.isValid()) {
            img2Result.buffer = convertFormat(std::move(img2Result.buffer),
                                              PixelFormatIDs::RGBA8_Straight);
        }

        // ========================================
        // Step 2: Union領域を計算
        // ========================================
        // img1とimg2の有効領域のUnion（和集合）を求める
        // 座標系: 基準点からの相対座標（ワールド座標）

        int_fixed unionMinX, unionMinY, unionMaxX, unionMaxY;
        bool hasUnion = false;

        auto updateUnion = [&](const RenderResult& result) {
            if (!result.isValid()) return;
            ViewPort v = result.view();
            // ワールド座標での領域（基準点相対）
            int_fixed minX = result.origin.x - to_fixed(v.width);
            int_fixed minY = result.origin.y - to_fixed(v.height);
            int_fixed maxX = result.origin.x;
            int_fixed maxY = result.origin.y;

            if (!hasUnion) {
                unionMinX = minX;
                unionMinY = minY;
                unionMaxX = maxX;
                unionMaxY = maxY;
                hasUnion = true;
            } else {
                // Union: 両方を含む最小矩形
                if (minX < unionMinX) unionMinX = minX;
                if (minY < unionMinY) unionMinY = minY;
                if (maxX > unionMaxX) unionMaxX = maxX;
                if (maxY > unionMaxY) unionMaxY = maxY;
            }
        };

        updateUnion(img1Result);
        updateUnion(img2Result);

        // Unionサイズを計算
        int unionWidth = from_fixed(unionMaxX - unionMinX);
        int unionHeight = from_fixed(unionMaxY - unionMinY);
        int_fixed unionOriginX = unionMaxX;
        int_fixed unionOriginY = unionMaxY;

        // ========================================
        // Step 3: マスクを取得（Union領域で要求）
        // ========================================
        RenderResult maskResult;
        if (upstream2) {
            RenderRequest unionRequest;
            unionRequest.width = unionWidth;
            unionRequest.height = unionHeight;
            unionRequest.origin = Point{unionOriginX, unionOriginY};

            maskResult = upstream2->pullProcess(unionRequest);

            // マスクはAlpha8に変換
            if (maskResult.isValid()) {
                maskResult.buffer = convertFormat(std::move(maskResult.buffer),
                                                  PixelFormatIDs::Alpha8);
            }
        }

        // ========================================
        // Step 4: 出力バッファを確保（Union領域サイズ）
        // ========================================
        ImageBuffer outputBuf(unionWidth, unionHeight, PixelFormatIDs::RGBA8_Straight);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Matte].recordAlloc(
            outputBuf.totalBytes(), outputBuf.width(), outputBuf.height());
#endif

        // マット合成処理（Union領域で実行）
        RenderRequest unionRequest;
        unionRequest.width = unionWidth;
        unionRequest.height = unionHeight;
        unionRequest.origin = Point{unionOriginX, unionOriginY};

        applyMatteComposite(outputBuf, unionRequest,
                           img1Result, img2Result, maskResult);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& metrics = PerfMetrics::instance().nodes[NodeType::Matte];
        metrics.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - startTime).count();
        metrics.count++;
#endif

        return RenderResult(std::move(outputBuf), Point{unionOriginX, unionOriginY});
    }

private:
    // 255での除算を高速化するマクロ（誤差なし）
    // (x * 257 + 256) >> 16 は x / 255 と等価（0-65025の範囲で）
    static inline uint8_t div255(uint32_t x) {
        return static_cast<uint8_t>((x * 257 + 256) >> 16);
    }

    // マット合成の実処理（最適化版）
    void applyMatteComposite(ImageBuffer& output, const RenderRequest& request,
                             const RenderResult& img1, const RenderResult& img2,
                             const RenderResult& mask) {
        ViewPort outView = output.view();
        uint8_t* outPtr = static_cast<uint8_t*>(outView.data);
        const int outStride = outView.stride;
        const int outWidth = request.width;
        const int outHeight = request.height;

        // ========================================
        // 最適化1: 座標オフセットの事前計算
        // ========================================
        // 入力座標 = 出力ピクセル - from_fixed(outOrigin) + from_fixed(inputOrigin)
        // オフセット = from_fixed(inputOrigin - outOrigin)
        const int_fixed outOriginX = request.origin.x;
        const int_fixed outOriginY = request.origin.y;

        // 各入力のビュー情報
        const uint8_t* img1Ptr = nullptr;
        const uint8_t* img2Ptr = nullptr;
        const uint8_t* maskPtr = nullptr;
        int img1Width = 0, img1Height = 0, img1Stride = 0;
        int img2Width = 0, img2Height = 0, img2Stride = 0;
        int maskWidth = 0, maskHeight = 0, maskStride = 0;
        int img1OffsetX = 0, img1OffsetY = 0;
        int img2OffsetX = 0, img2OffsetY = 0;
        int maskOffsetX = 0, maskOffsetY = 0;

        if (img1.isValid()) {
            ViewPort v = img1.view();
            img1Ptr = static_cast<const uint8_t*>(v.data);
            img1Width = v.width;
            img1Height = v.height;
            img1Stride = v.stride;
            img1OffsetX = from_fixed(img1.origin.x - outOriginX);
            img1OffsetY = from_fixed(img1.origin.y - outOriginY);
        }
        if (img2.isValid()) {
            ViewPort v = img2.view();
            img2Ptr = static_cast<const uint8_t*>(v.data);
            img2Width = v.width;
            img2Height = v.height;
            img2Stride = v.stride;
            img2OffsetX = from_fixed(img2.origin.x - outOriginX);
            img2OffsetY = from_fixed(img2.origin.y - outOriginY);
        }
        if (mask.isValid()) {
            ViewPort v = mask.view();
            maskPtr = static_cast<const uint8_t*>(v.data);
            maskWidth = v.width;
            maskHeight = v.height;
            maskStride = v.stride;
            maskOffsetX = from_fixed(mask.origin.x - outOriginX);
            maskOffsetY = from_fixed(mask.origin.y - outOriginY);
        }

        // ========================================
        // 最適化2: 特殊ケースの高速パス
        // ========================================

        // ケース: マスクがない → 全面背景（alpha=0）
        if (!maskPtr) {
            if (!img2Ptr) {
                // 背景もない → 全面透明黒
                std::memset(outPtr, 0, outHeight * outStride);
                return;
            }
            // 背景のみコピー
            copyImageToOutput(outPtr, outStride, outWidth, outHeight,
                              img2Ptr, img2Stride, img2Width, img2Height,
                              img2OffsetX, img2OffsetY);
            return;
        }

        // ========================================
        // 最適化3: ランレングス処理
        // ========================================
        // マスク値の連続領域を検出し、0/255の連続はmemcpyで高速処理

        for (int y = 0; y < outHeight; ++y) {
            uint8_t* outRow = outPtr + y * outStride;

            // 各入力の現在行のY座標と有効性チェック
            const int img1Y = y + img1OffsetY;
            const int img2Y = y + img2OffsetY;
            const int maskY = y + maskOffsetY;

            // 行ポインタ（Y範囲外ならnullptr）
            const uint8_t* img1Row = (static_cast<unsigned>(img1Y) < static_cast<unsigned>(img1Height))
                                     ? img1Ptr + img1Y * img1Stride : nullptr;
            const uint8_t* img2Row = (static_cast<unsigned>(img2Y) < static_cast<unsigned>(img2Height))
                                     ? img2Ptr + img2Y * img2Stride : nullptr;
            const uint8_t* maskRow = (static_cast<unsigned>(maskY) < static_cast<unsigned>(maskHeight))
                                     ? maskPtr + maskY * maskStride : nullptr;

            // マスク行が無効な場合は全面alpha=0（背景のみ）
            if (!maskRow) {
                copyRowRegion(outRow, img2Row, img2OffsetX, img2Width, 0, outWidth);
                continue;
            }

            // マスクの有効X範囲
            const int maskXStart = std::max(0, -maskOffsetX);
            const int maskXEnd = std::min(outWidth, maskWidth - maskOffsetX);

            // マスク範囲外の左側（alpha=0）
            if (maskXStart > 0) {
                copyRowRegion(outRow, img2Row, img2OffsetX, img2Width, 0, maskXStart);
            }

            // マスク有効範囲内をランレングス処理
            int x = maskXStart;
            uint8_t currentAlpha = (x < maskXEnd) ? maskRow[x + maskOffsetX] : 0;

            while (x < maskXEnd) {
                const uint8_t runAlpha = currentAlpha;
                const int runStart = x;

                // 同じalpha値が続く限り進む
                while (x < maskXEnd) {
                    x++;
                    if (x < maskXEnd) {
                        currentAlpha = maskRow[x + maskOffsetX];
                        if (currentAlpha != runAlpha) break;
                    }
                }
                const int runLength = x - runStart;

                if (runAlpha == 0) {
                    // 背景のみコピー
                    copyRowRegion(outRow, img2Row, img2OffsetX, img2Width, runStart, runStart + runLength);
                } else if (runAlpha == 255) {
                    // 前景のみコピー
                    copyRowRegion(outRow, img1Row, img1OffsetX, img1Width, runStart, runStart + runLength);
                } else {
                    // 中間値: 同じalpha値で複数ピクセルを一括ブレンド
                    blendPixels(outRow, runStart, runLength, runAlpha,
                                img1Row, img1OffsetX, img1Width,
                                img2Row, img2OffsetX, img2Width);
                }
            }

            // マスク範囲外の右側（alpha=0）
            if (maskXEnd < outWidth) {
                copyRowRegion(outRow, img2Row, img2OffsetX, img2Width, maskXEnd, outWidth);
            }
        }
    }

    // 行の一部領域をコピー（alpha=0またはalpha=255用）
    void copyRowRegion(uint8_t* outRow,
                       const uint8_t* srcRow, int srcOffsetX, int srcWidth,
                       int xStart, int xEnd) {
        if (!srcRow) {
            // ソースがない場合は透明黒
            std::memset(outRow + xStart * 4, 0, (xEnd - xStart) * 4);
            return;
        }

        // ソースの有効X範囲と出力範囲の交差を計算
        const int srcXStart = std::max(xStart, -srcOffsetX);
        const int srcXEnd = std::min(xEnd, srcWidth - srcOffsetX);

        // 左側の透明部分
        if (srcXStart > xStart) {
            std::memset(outRow + xStart * 4, 0, (srcXStart - xStart) * 4);
        }

        // 有効部分をコピー
        if (srcXEnd > srcXStart) {
            std::memcpy(outRow + srcXStart * 4,
                        srcRow + (srcXStart + srcOffsetX) * 4,
                        (srcXEnd - srcXStart) * 4);
        }

        // 右側の透明部分
        if (srcXEnd < xEnd) {
            std::memset(outRow + srcXEnd * 4, 0, (xEnd - srcXEnd) * 4);
        }
    }

    // 複数ピクセルの一括ブレンド処理（同一alpha値）
    void blendPixels(uint8_t* outRow, int xStart, int length, uint8_t alpha,
                     const uint8_t* img1Row, int img1OffsetX, int img1Width,
                     const uint8_t* img2Row, int img2OffsetX, int img2Width) {
        // alpha/inv_alphaを1回だけ計算
        const uint32_t a = alpha;
        const uint32_t inv_a = 255 - alpha;

        for (int i = 0; i < length; ++i) {
            const int x = xStart + i;
            uint8_t* outP = outRow + x * 4;

            // 読み出し時にalpha適用（範囲外は乗算スキップ）
            uint32_t r1a = 0, g1a = 0, b1a = 0, a1a = 0;
            uint32_t r2a = 0, g2a = 0, b2a = 0, a2a = 0;

            // 前景から取得（alpha適用済み）
            const int img1X = x + img1OffsetX;
            if (img1Row && static_cast<unsigned>(img1X) < static_cast<unsigned>(img1Width)) {
                const uint8_t* p = img1Row + img1X * 4;
                r1a = p[0] * a; g1a = p[1] * a; b1a = p[2] * a; a1a = p[3] * a;
            }

            // 背景から取得（inv_alpha適用済み）
            const int img2X = x + img2OffsetX;
            if (img2Row && static_cast<unsigned>(img2X) < static_cast<unsigned>(img2Width)) {
                const uint8_t* p = img2Row + img2X * 4;
                r2a = p[0] * inv_a; g2a = p[1] * inv_a; b2a = p[2] * inv_a; a2a = p[3] * inv_a;
            }

            // ブレンド（加算のみ）
            outP[0] = div255(r1a + r2a);
            outP[1] = div255(g1a + g2a);
            outP[2] = div255(b1a + b2a);
            outP[3] = div255(a1a + a2a);
        }
    }

    // 画像を出力バッファにコピー（高速パス用）
    void copyImageToOutput(uint8_t* outPtr, int outStride, int outWidth, int outHeight,
                           const uint8_t* srcPtr, int srcStride, int srcWidth, int srcHeight,
                           int offsetX, int offsetY) {
        for (int y = 0; y < outHeight; ++y) {
            uint8_t* outRow = outPtr + y * outStride;
            const int srcY = y + offsetY;

            if (srcY < 0 || srcY >= srcHeight) {
                // 範囲外 → 透明黒
                std::memset(outRow, 0, outWidth * 4);
                continue;
            }

            const uint8_t* srcRow = srcPtr + srcY * srcStride;

            // X範囲を計算
            const int xStart = std::max(0, -offsetX);
            const int xEnd = std::min(outWidth, srcWidth - offsetX);

            // 左側の透明部分
            if (xStart > 0) {
                std::memset(outRow, 0, xStart * 4);
            }

            // 有効部分をコピー
            if (xEnd > xStart) {
                std::memcpy(outRow + xStart * 4,
                            srcRow + (xStart + offsetX) * 4,
                            (xEnd - xStart) * 4);
            }

            // 右側の透明部分
            if (xEnd < outWidth) {
                std::memset(outRow + xEnd * 4, 0, (outWidth - xEnd) * 4);
            }
        }
    }

    // Union領域にクリッピングした結果を作成（マスクなし時の高速パス）
    RenderResult createClippedResult(const RenderResult& src,
                                     int_fixed unionOriginX, int_fixed unionOriginY,
                                     int unionWidth, int unionHeight) {
        ImageBuffer outputBuf(unionWidth, unionHeight, PixelFormatIDs::RGBA8_Straight);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Matte].recordAlloc(
            outputBuf.totalBytes(), outputBuf.width(), outputBuf.height());
#endif

        ViewPort srcView = src.view();
        const uint8_t* srcPtr = static_cast<const uint8_t*>(srcView.data);
        const int srcStride = srcView.stride;
        const int srcWidth = srcView.width;
        const int srcHeight = srcView.height;

        ViewPort outView = outputBuf.view();
        uint8_t* outPtr = static_cast<uint8_t*>(outView.data);
        const int outStride = outView.stride;

        // オフセット計算
        const int offsetX = from_fixed(src.origin.x - unionOriginX);
        const int offsetY = from_fixed(src.origin.y - unionOriginY);

        copyImageToOutput(outPtr, outStride, unionWidth, unionHeight,
                          srcPtr, srcStride, srcWidth, srcHeight,
                          offsetX, offsetY);

        return RenderResult(std::move(outputBuf), Point{unionOriginX, unionOriginY});
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_MATTE_NODE_H
