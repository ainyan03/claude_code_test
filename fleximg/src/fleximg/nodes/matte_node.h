#ifndef FLEXIMG_MATTE_NODE_H
#define FLEXIMG_MATTE_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../image/pixel_format.h"
#include "../operations/canvas_utils.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// MatteNode - マット合成ノード
// ========================================================================
//
// 3つの入力画像を使ってマット合成（アルファマスク合成）を行います。
// - 入力ポート0: 前景（マスク白部分に表示）
// - 入力ポート1: 背景（マスク黒部分に表示）
// - 入力ポート2: アルファマスク（Alpha8推奨）
// - 出力: 1ポート
//
// 計算式:
//   Output = Foreground × Alpha + Background × (1 - Alpha)
//
// 未接続・範囲外の扱い:
// - 前景/背景: 透明の黒 (0,0,0,0)
// - アルファマスク: alpha=0（全面背景）
//
// 最適化:
// - 処理順序: マスク → 背景 → 前景（マスク結果に応じて前景要求を最適化）
// - マスクが空または全面0の場合: 背景をそのまま返す（早期リターン）
// - マスクの有効範囲スキャン: 左右の0連続領域を除外し、前景要求範囲を縮小
// - ランレングス処理: 同一alpha値の連続区間をまとめて処理
// - alpha=0/255の特殊ケース: memcpy/memsetで高速処理
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

protected:
    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: 全上流ノードにPrepareRequestを伝播
    PrepareResponse onPullPrepare(const PrepareRequest& request) override;

    // onPullFinalize: 全上流ノードに終了を伝播
    void onPullFinalize() override;

    // onPullProcess: マット合成処理
    RenderResponse onPullProcess(const RenderRequest& request) override;

private:
    // 255での除算を高速化（誤差なし）
    // (x * 257 + 256) >> 16 は x / 255 と等価（0-65025の範囲で）
    static inline uint8_t div255(uint32_t x) {
        return static_cast<uint8_t>((x * 257 + 256) >> 16);
    }

    // マット合成の実処理（最適化版）
    void applyMatteComposite(ImageBuffer& output, const RenderRequest& request,
                             const RenderResponse& fg, const RenderResponse& bg,
                             const RenderResponse& mask);

    // 行の一部領域をコピー（alpha=0またはalpha=255用）
    void copyRowRegion(uint8_t* outRow,
                       const uint8_t* srcRowBase, int srcOffsetX, int srcWidth,
                       int xStart, int xEnd);

    // 最適化版ブレンド処理（範囲事前計算で境界チェックを削減）
    void blendPixelsOptimized(uint8_t* outRow, int xStart, int xEnd, uint8_t alpha,
                              const uint8_t* fgRowBase, int fgOffsetX, int fgWidth,
                              const uint8_t* bgRowBase, int bgOffsetX, int bgWidth);

    // 画像を出力バッファにコピー（高速パス用、スキャンライン = height==1 前提）
    void copyImageToOutput(uint8_t* outPtr, int outWidth,
                           const uint8_t* srcPtr, int srcStride, int srcWidth, int srcHeight,
                           int offsetX, int offsetY);

    // Union領域にクリッピングした結果を作成（マスクなし時の高速パス）
    RenderResponse createClippedResult(const RenderResponse& src,
                                     int_fixed unionOriginX, int_fixed unionOriginY,
                                     int unionWidth, int unionHeight);
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// MatteNode - Template Method フック実装
// ============================================================================

PrepareResponse MatteNode::onPullPrepare(const PrepareRequest& request) {
    PrepareResponse merged;
    merged.status = PrepareStatus::Prepared;
    bool hasValidUpstream = false;

    // AABB和集合計算用（ワールド座標）
    float minX = 0, minY = 0, maxX = 0, maxY = 0;

    // 全上流へ伝播し、結果をマージ（AABB和集合）
    for (int i = 0; i < 3; ++i) {
        Node* upstream = upstreamNode(i);
        if (upstream) {
            PrepareResponse result = upstream->pullPrepare(request);
            if (!result.ok()) {
                return result;  // エラーを伝播
            }

            // 新座標系: originはバッファ左上のワールド座標
            float left = fixed_to_float(result.origin.x);
            float top = fixed_to_float(result.origin.y);
            float right = left + static_cast<float>(result.width);
            float bottom = top + static_cast<float>(result.height);

            if (!hasValidUpstream) {
                // 最初の結果でベースを初期化
                minX = left;
                minY = top;
                maxX = right;
                maxY = bottom;
                hasValidUpstream = true;
            } else {
                // 和集合（各辺のmin/max）
                if (left < minX) minX = left;
                if (top < minY) minY = top;
                if (right > maxX) maxX = right;
                if (bottom > maxY) maxY = bottom;
            }
        }
    }

    if (hasValidUpstream) {
        // 和集合結果をPrepareResponseに設定
        merged.width = static_cast<int16_t>(std::ceil(maxX - minX));
        merged.height = static_cast<int16_t>(std::ceil(maxY - minY));
        // 新座標系: originはバッファ左上のワールド座標
        merged.origin.x = float_to_fixed(minX);
        merged.origin.y = float_to_fixed(minY);
        // MatteNodeは常にRGBA8_Straightで出力
        merged.preferredFormat = PixelFormatIDs::RGBA8_Straight;
    } else {
        // 上流がない場合はサイズ0を返す
        // width/height/originはデフォルト値（0）のまま
    }

    // 準備処理
    RenderRequest screenInfo;
    screenInfo.width = request.width;
    screenInfo.height = request.height;
    screenInfo.origin = request.origin;
    prepare(screenInfo);

    return merged;
}

void MatteNode::onPullFinalize() {
    finalize();
    for (int i = 0; i < 3; ++i) {
        Node* upstream = upstreamNode(i);
        if (upstream) {
            upstream->pullFinalize();
        }
    }
}

RenderResponse MatteNode::onPullProcess(const RenderRequest& request) {
    // 注意: FLEXIMG_METRICS_SCOPEは上流呼び出し後に配置
    // （上流の処理時間を含めないため）

    Node* fgNode = upstreamNode(0);    // 前景 (foreground)
    Node* bgNode = upstreamNode(1);    // 背景 (background)
    Node* maskNode = upstreamNode(2);  // マスク

    // ========================================
    // Step 1: マスクを最初に評価（request範囲で要求）
    // ========================================
    RenderResponse maskResult;
    if (maskNode) {
        maskResult = maskNode->pullProcess(request);
        if (maskResult.isValid()) {
            maskResult.buffer = convertFormat(std::move(maskResult.buffer),
                                              PixelFormatIDs::Alpha8);
        }
    }

    // マスクが空の場合: 背景をそのまま返す（早期リターン）
    if (!maskResult.isValid()) {
        if (bgNode) {
            return bgNode->pullProcess(request);
        }
        // 空の結果でもoriginは維持
        return RenderResponse(ImageBuffer(), request.origin);
    }

    // ========================================
    // マスクの有効範囲をスキャン（スキャンライン = 高さ1前提）
    // ========================================
    ViewPort maskView = maskResult.view();
    const uint8_t* maskData = static_cast<const uint8_t*>(maskView.data);
    const int maskWidth = maskView.width;

    // 左端から0が連続する範囲
    int maskLeftSkip = 0;
    while (maskLeftSkip < maskWidth && maskData[maskLeftSkip] == 0) {
        maskLeftSkip++;
    }

    // 全て0なら早期リターン（背景のみ）
    if (maskLeftSkip >= maskWidth) {
        if (bgNode) {
            return bgNode->pullProcess(request);
        }
        return RenderResponse(ImageBuffer(), request.origin);
    }

    // 右端から0が連続する範囲
    int maskRightSkip = 0;
    while (maskRightSkip < maskWidth - maskLeftSkip &&
           maskData[maskWidth - 1 - maskRightSkip] == 0) {
        maskRightSkip++;
    }

    // 有効範囲
    const int maskEffectiveWidth = maskWidth - maskLeftSkip - maskRightSkip;

    // ========================================
    // Step 2: 背景を評価（request範囲で要求）
    // ========================================
    RenderResponse bgResult;
    if (bgNode) {
        bgResult = bgNode->pullProcess(request);
        if (bgResult.isValid()) {
            bgResult.buffer = convertFormat(std::move(bgResult.buffer),
                                            PixelFormatIDs::RGBA8_Straight);
        }
    }

    // ========================================
    // Step 3: 出力領域を計算（背景 ∪ マスク）
    // ========================================
    int_fixed unionMinX, unionMinY, unionMaxX, unionMaxY;
    bool hasUnion = false;

    auto updateUnion = [&](const RenderResponse& result) {
        if (!result.isValid()) return;
        ViewPort v = result.view();
        // 新座標系: originはバッファ左上のワールド座標
        int_fixed minX = result.origin.x;
        int_fixed minY = result.origin.y;
        int_fixed maxX = result.origin.x + to_fixed(v.width);
        int_fixed maxY = result.origin.y + to_fixed(v.height);

        if (!hasUnion) {
            unionMinX = minX;
            unionMinY = minY;
            unionMaxX = maxX;
            unionMaxY = maxY;
            hasUnion = true;
        } else {
            if (minX < unionMinX) unionMinX = minX;
            if (minY < unionMinY) unionMinY = minY;
            if (maxX > unionMaxX) unionMaxX = maxX;
            if (maxY > unionMaxY) unionMaxY = maxY;
        }
    };

    updateUnion(maskResult);  // マスク範囲
    updateUnion(bgResult);    // 背景範囲

    // Union領域がない場合（両方空）
    if (!hasUnion) {
        // 空の結果でもoriginは維持
        return RenderResponse(ImageBuffer(), request.origin);
    }

    int unionWidth = from_fixed(unionMaxX - unionMinX);
    int unionHeight = from_fixed(unionMaxY - unionMinY);
    // 新座標系: originはバッファ左上のワールド座標
    int_fixed unionOriginX = unionMinX;
    int_fixed unionOriginY = unionMinY;

    // ========================================
    // Step 4: 前景を評価（マスク有効範囲で要求）← 最適化ポイント
    // ========================================
    RenderResponse fgResult;
    if (fgNode) {
        // マスクの有効範囲でのみ前景を要求
        // 新座標系: originはバッファ左上のワールド座標なので、
        // 左端がleftSkip分右にずれる → origin.xをleftSkip分増やす
        RenderRequest fgRequest;
        fgRequest.width = static_cast<int16_t>(maskEffectiveWidth);
        fgRequest.height = maskView.height;
        fgRequest.origin.x = maskResult.origin.x + to_fixed(maskLeftSkip);
        fgRequest.origin.y = maskResult.origin.y;

        fgResult = fgNode->pullProcess(fgRequest);
        if (fgResult.isValid()) {
            fgResult.buffer = convertFormat(std::move(fgResult.buffer),
                                            PixelFormatIDs::RGBA8_Straight);
        }
    }

    // ========================================
    // Step 5: 出力バッファを確保しマット合成
    // ========================================
    // ここからMatteNode自身の処理を計測（上流呼び出しは計測対象外）
    FLEXIMG_METRICS_SCOPE(NodeType::Matte);

    ImageBuffer outputBuf(unionWidth, unionHeight, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized, allocator_);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().nodes[NodeType::Matte].recordAlloc(
        outputBuf.totalBytes(), outputBuf.width(), outputBuf.height());
#endif

    RenderRequest unionRequest;
    unionRequest.width = static_cast<int16_t>(unionWidth);
    unionRequest.height = static_cast<int16_t>(unionHeight);
    unionRequest.origin = Point{unionOriginX, unionOriginY};

    applyMatteComposite(outputBuf, unionRequest,
                       fgResult, bgResult, maskResult);

    return RenderResponse(std::move(outputBuf), Point{unionOriginX, unionOriginY});
}

// ============================================================================
// MatteNode - private ヘルパーメソッド実装
// ============================================================================

void MatteNode::applyMatteComposite(ImageBuffer& output, const RenderRequest& request,
                                    const RenderResponse& fg, const RenderResponse& bg,
                                    const RenderResponse& mask) {
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
    const uint8_t* fgPtr = nullptr;
    const uint8_t* bgPtr = nullptr;
    const uint8_t* maskPtr = nullptr;
    int fgWidth = 0, fgHeight = 0, fgStride = 0;
    int bgWidth = 0, bgHeight = 0, bgStride = 0;
    int maskWidth = 0, maskHeight = 0, maskStride = 0;
    int fgOffsetX = 0, fgOffsetY = 0;
    int bgOffsetX = 0, bgOffsetY = 0;
    int maskOffsetX = 0, maskOffsetY = 0;

    if (fg.isValid()) {
        ViewPort v = fg.view();
        fgPtr = static_cast<const uint8_t*>(v.data);
        fgWidth = v.width;
        fgHeight = v.height;
        fgStride = v.stride;
        fgOffsetX = from_fixed(fg.origin.x - outOriginX);
        fgOffsetY = from_fixed(fg.origin.y - outOriginY);
    }
    if (bg.isValid()) {
        ViewPort v = bg.view();
        bgPtr = static_cast<const uint8_t*>(v.data);
        bgWidth = v.width;
        bgHeight = v.height;
        bgStride = v.stride;
        bgOffsetX = from_fixed(bg.origin.x - outOriginX);
        bgOffsetY = from_fixed(bg.origin.y - outOriginY);
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
        if (!bgPtr) {
            // 背景もない → 全面透明黒
            std::memset(outPtr, 0, static_cast<size_t>(outHeight) * static_cast<size_t>(outStride));
            return;
        }
        // 背景のみコピー
        copyImageToOutput(outPtr, outWidth,
                          bgPtr, bgStride, bgWidth, bgHeight,
                          bgOffsetX, bgOffsetY);
        return;
    }

    // ========================================
    // 最適化3: ランレングス処理（スキャンライン = height==1 前提）
    // ========================================
    // マスク値の連続領域を検出し、0/255の連続はmemcpyで高速処理

    // height==1前提: ループ不要、y=0固定
    (void)outHeight;  // 未使用警告抑制

    uint8_t* outRow = outPtr;

    // 行ポインタ（Y範囲外ならnullptr）
    const uint8_t* fgRowBase = (static_cast<unsigned>(fgOffsetY) < static_cast<unsigned>(fgHeight))
                               ? fgPtr + fgOffsetY * fgStride : nullptr;
    const uint8_t* bgRowBase = (static_cast<unsigned>(bgOffsetY) < static_cast<unsigned>(bgHeight))
                               ? bgPtr + bgOffsetY * bgStride : nullptr;
    const uint8_t* maskRowBase = (static_cast<unsigned>(maskOffsetY) < static_cast<unsigned>(maskHeight))
                                 ? maskPtr + maskOffsetY * maskStride : nullptr;

    // マスク行が無効な場合は全面alpha=0（背景のみ）
    if (!maskRowBase) {
        copyRowRegion(outRow, bgRowBase, bgOffsetX, bgWidth, 0, outWidth);
        return;
    }

    // マスクの有効X範囲（出力座標系）
    // 新座標系: maskOffsetX = マスク左上 - 出力左上（出力座標系でのマスク左端位置）
    const int maskXStart = std::max(0, maskOffsetX);
    const int maskXEnd = std::min(outWidth, maskWidth + maskOffsetX);

    // マスク範囲外の左側（alpha=0）→背景のみ
    if (maskXStart > 0) {
        copyRowRegion(outRow, bgRowBase, bgOffsetX, bgWidth, 0, maskXStart);
    }

    // オフセット適用済みポインタ（ループ内でのオフセット計算を削減）
    // 新座標系: 出力のmaskXStartに対応するマスクのインデックス = maskXStart - maskOffsetX
    const uint8_t* maskP = maskRowBase + (maskXStart - maskOffsetX);
    const uint8_t* const maskPEnd = maskRowBase + (maskXEnd - maskOffsetX);

    int x = maskXStart;

    // ランレングス処理ループ
    while (maskP < maskPEnd) {
        const uint8_t runAlpha = *maskP;
        const int runStart = x;

        // 同じalpha値が続く限り進む
        do {
            ++maskP;
            ++x;
        } while (maskP < maskPEnd && *maskP == runAlpha);

        const int runEnd = x;

        if (runAlpha == 0) {
            // 背景のみコピー
            copyRowRegion(outRow, bgRowBase, bgOffsetX, bgWidth, runStart, runEnd);
        } else if (runAlpha == 255) {
            // 前景のみコピー
            copyRowRegion(outRow, fgRowBase, fgOffsetX, fgWidth, runStart, runEnd);
        } else {
            // 中間値: ブレンド処理
            blendPixelsOptimized(outRow, runStart, runEnd, runAlpha,
                                 fgRowBase, fgOffsetX, fgWidth,
                                 bgRowBase, bgOffsetX, bgWidth);
        }
    }

    // マスク範囲外の右側（alpha=0）→背景のみ
    if (maskXEnd < outWidth) {
        copyRowRegion(outRow, bgRowBase, bgOffsetX, bgWidth, maskXEnd, outWidth);
    }
}

void MatteNode::copyRowRegion(uint8_t* outRow,
                              const uint8_t* srcRowBase, int srcOffsetX, int srcWidth,
                              int xStart, int xEnd) {
    if (!srcRowBase) {
        // ソースがない場合は透明黒
        std::memset(outRow + xStart * 4, 0, static_cast<size_t>(xEnd - xStart) * 4);
        return;
    }

    // ソースの有効X範囲と出力範囲の交差を計算
    // 新座標系: srcOffsetX = ソース左上 - 出力左上（出力座標系でのソース左端位置）
    const int srcXStart = std::max(xStart, srcOffsetX);
    const int srcXEnd = std::min(xEnd, srcWidth + srcOffsetX);

    // 左側の透明部分
    if (srcXStart > xStart) {
        std::memset(outRow + xStart * 4, 0, static_cast<size_t>(srcXStart - xStart) * 4);
    }

    // 有効部分をコピー
    if (srcXEnd > srcXStart) {
        std::memcpy(outRow + srcXStart * 4,
                    srcRowBase + (srcXStart - srcOffsetX) * 4,
                    static_cast<size_t>(srcXEnd - srcXStart) * 4);
    }

    // 右側の透明部分
    const int clearStart = std::max(srcXEnd, xStart);
    if (clearStart < xEnd) {
        std::memset(outRow + clearStart * 4, 0, static_cast<size_t>(xEnd - clearStart) * 4);
    }
}

void MatteNode::blendPixelsOptimized(uint8_t* outRow, int xStart, int xEnd, uint8_t alpha,
                                     const uint8_t* fgRowBase, int fgOffsetX, int fgWidth,
                                     const uint8_t* bgRowBase, int bgOffsetX, int bgWidth) {
    const uint32_t a = alpha;
    const uint32_t inv_a = 255 - alpha;

    // 前景・背景の有効X範囲を事前計算
    // 新座標系: offsetX = ソース左上 - 出力左上（出力座標系でのソース左端位置）
    const int fgXStart = fgRowBase ? std::max(xStart, fgOffsetX) : xEnd;
    const int fgXEnd = fgRowBase ? std::min(xEnd, fgWidth + fgOffsetX) : xStart;
    const int bgXStart = bgRowBase ? std::max(xStart, bgOffsetX) : xEnd;
    const int bgXEnd = bgRowBase ? std::min(xEnd, bgWidth + bgOffsetX) : xStart;

    // オフセット適用済みポインタ
    uint8_t* outP = outRow + xStart * 4;
    const uint8_t* fgP = fgRowBase ? fgRowBase + (xStart - fgOffsetX) * 4 : nullptr;
    const uint8_t* bgP = bgRowBase ? bgRowBase + (xStart - bgOffsetX) * 4 : nullptr;

    for (int x = xStart; x < xEnd; ++x) {
        uint32_t fgR = 0, fgG = 0, fgB = 0, fgA = 0;
        uint32_t bgR = 0, bgG = 0, bgB = 0, bgA = 0;

        // 前景（範囲内のみ）
        if (x >= fgXStart && x < fgXEnd) {
            fgR = fgP[0] * a; fgG = fgP[1] * a; fgB = fgP[2] * a; fgA = fgP[3] * a;
        }

        // 背景（範囲内のみ）
        if (x >= bgXStart && x < bgXEnd) {
            bgR = bgP[0] * inv_a; bgG = bgP[1] * inv_a; bgB = bgP[2] * inv_a; bgA = bgP[3] * inv_a;
        }

        outP[0] = div255(fgR + bgR);
        outP[1] = div255(fgG + bgG);
        outP[2] = div255(fgB + bgB);
        outP[3] = div255(fgA + bgA);

        outP += 4;
        if (fgP) fgP += 4;
        if (bgP) bgP += 4;
    }
}

void MatteNode::copyImageToOutput(uint8_t* outPtr, int outWidth,
                                  const uint8_t* srcPtr, int srcStride, int srcWidth, int srcHeight,
                                  int offsetX, int offsetY) {
    uint8_t* outRow = outPtr;
    const int srcY = offsetY;  // y=0 + offsetY

    if (srcY < 0 || srcY >= srcHeight) {
        // 範囲外 → 透明黒
        std::memset(outRow, 0, static_cast<size_t>(outWidth) * 4);
        return;
    }

    const uint8_t* srcRow = srcPtr + srcY * srcStride;

    // X範囲を計算
    // 新座標系: offsetX = ソース左上 - 出力左上（出力座標系でのソース左端位置）
    const int xStart = std::max(0, offsetX);
    const int xEnd = std::min(outWidth, srcWidth + offsetX);

    // 左側の透明部分
    if (xStart > 0) {
        std::memset(outRow, 0, static_cast<size_t>(xStart) * 4);
    }

    // 有効部分をコピー
    if (xEnd > xStart) {
        std::memcpy(outRow + xStart * 4,
                    srcRow + (xStart - offsetX) * 4,
                    static_cast<size_t>(xEnd - xStart) * 4);
    }

    // 右側の透明部分
    if (xEnd < outWidth) {
        std::memset(outRow + xEnd * 4, 0, static_cast<size_t>(outWidth - xEnd) * 4);
    }
}

RenderResponse MatteNode::createClippedResult(const RenderResponse& src,
                                            int_fixed unionOriginX, int_fixed unionOriginY,
                                            int unionWidth, int unionHeight) {
    ImageBuffer outputBuf(unionWidth, unionHeight, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized, allocator_);

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

    // オフセット計算
    const int offsetX = from_fixed(src.origin.x - unionOriginX);
    const int offsetY = from_fixed(src.origin.y - unionOriginY);

    copyImageToOutput(outPtr, unionWidth,
                      srcPtr, srcStride, srcWidth, srcHeight,
                      offsetX, offsetY);

    return RenderResponse(std::move(outputBuf), Point{unionOriginX, unionOriginY});
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_MATTE_NODE_H
