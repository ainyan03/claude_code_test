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
    // ========================================
    // ヘルパー構造体・関数
    // ========================================

    // 入力画像のビュー情報（座標変換済み）
    struct InputView {
        const uint8_t* ptr = nullptr;
        int width = 0, height = 0, stride = 0;
        int offsetX = 0, offsetY = 0;

        bool valid() const { return ptr != nullptr; }

        // 指定Y座標の行ポインタ（範囲外ならnullptr）
        const uint8_t* rowAt(int y) const {
            int srcY = y - offsetY;
            if (static_cast<unsigned>(srcY) >= static_cast<unsigned>(height)) return nullptr;
            return ptr + srcY * stride;
        }

        // RenderResponseから構築
        static InputView from(const RenderResponse& resp, int_fixed outOriginX, int_fixed outOriginY) {
            InputView v;
            if (!resp.isValid()) return v;
            ViewPort vp = resp.view();
            v.ptr = static_cast<const uint8_t*>(vp.data);
            v.width = vp.width;
            v.height = vp.height;
            v.stride = vp.stride;
            v.offsetX = from_fixed(resp.origin.x - outOriginX);
            v.offsetY = from_fixed(resp.origin.y - outOriginY);
            return v;
        }
    };

    // マスクの左右0スキップ範囲をスキャン（4バイト単位、アライメント対応）
    // 戻り値: 有効範囲の幅（0なら全面0）
    static int scanMaskZeroRanges(const uint8_t* maskData, int maskWidth,
                                  int& outLeftSkip, int& outRightSkip);

    // ========================================
    // 合成処理
    // ========================================

    // マット合成の実処理（スキャンライン単位、height==1前提）
    void applyMatteComposite(ImageBuffer& output, int outWidth,
                             const InputView& fg, const InputView& bg, const InputView& mask);

    // 行の一部領域をコピー（RGBA8、alpha=0/255用）
    static void copyRowRegion(uint8_t* outRow,
                              const uint8_t* srcRowBase, int srcOffsetX, int srcWidth,
                              int xStart, int xEnd);

    // ブレンド処理（中間alpha値用）
    static void blendPixels(uint8_t* outRow, int xStart, int xEnd, uint8_t alpha,
                            const uint8_t* fgRowBase, int fgOffsetX, int fgWidth,
                            const uint8_t* bgRowBase, int bgOffsetX, int bgWidth);
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
    Node* fgNode = upstreamNode(0);    // 前景
    Node* bgNode = upstreamNode(1);    // 背景
    Node* maskNode = upstreamNode(2);  // マスク

    // Step 1: マスク取得
    RenderResponse maskResult;
    if (maskNode) {
        maskResult = maskNode->pullProcess(request);
        if (maskResult.isValid()) {
            maskResult.buffer = convertFormat(std::move(maskResult.buffer),
                                              PixelFormatIDs::Alpha8);
        }
    }

    // マスクなし → 背景をそのまま返す
    if (!maskResult.isValid()) {
        return bgNode ? bgNode->pullProcess(request)
                      : RenderResponse(ImageBuffer(), request.origin);
    }

    // Step 2: マスクの有効範囲をスキャン
    ViewPort maskView = maskResult.view();
    const uint8_t* maskData = static_cast<const uint8_t*>(maskView.data);
    int maskLeftSkip = 0, maskRightSkip = 0;
    int maskEffectiveWidth = scanMaskZeroRanges(maskData, maskView.width,
                                                 maskLeftSkip, maskRightSkip);

    // 全面0 → 背景をそのまま返す
    if (maskEffectiveWidth == 0) {
        return bgNode ? bgNode->pullProcess(request)
                      : RenderResponse(ImageBuffer(), request.origin);
    }

    // Step 3: 背景取得
    RenderResponse bgResult;
    if (bgNode) {
        bgResult = bgNode->pullProcess(request);
        if (bgResult.isValid()) {
            bgResult.buffer = convertFormat(std::move(bgResult.buffer),
                                            PixelFormatIDs::RGBA8_Straight);
        }
    }

    // Step 4: 出力領域計算（マスク ∪ 背景）
    int_fixed unionMinX = maskResult.origin.x;
    int_fixed unionMinY = maskResult.origin.y;
    int_fixed unionMaxX = unionMinX + to_fixed(maskView.width);
    int_fixed unionMaxY = unionMinY + to_fixed(maskView.height);

    if (bgResult.isValid()) {
        ViewPort bgView = bgResult.view();
        int_fixed bgMinX = bgResult.origin.x;
        int_fixed bgMinY = bgResult.origin.y;
        int_fixed bgMaxX = bgMinX + to_fixed(bgView.width);
        int_fixed bgMaxY = bgMinY + to_fixed(bgView.height);
        if (bgMinX < unionMinX) unionMinX = bgMinX;
        if (bgMinY < unionMinY) unionMinY = bgMinY;
        if (bgMaxX > unionMaxX) unionMaxX = bgMaxX;
        if (bgMaxY > unionMaxY) unionMaxY = bgMaxY;
    }

    int unionWidth = from_fixed(unionMaxX - unionMinX);
    int unionHeight = from_fixed(unionMaxY - unionMinY);

    // Step 5: 前景取得（マスク有効範囲のみ）
    RenderResponse fgResult;
    if (fgNode) {
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

    // Step 6: 合成
    FLEXIMG_METRICS_SCOPE(NodeType::Matte);

    ImageBuffer outputBuf(unionWidth, unionHeight, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized, allocator_);

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().nodes[NodeType::Matte].recordAlloc(
        outputBuf.totalBytes(), outputBuf.width(), outputBuf.height());
#endif

    // InputViewを構築
    InputView fgView = InputView::from(fgResult, unionMinX, unionMinY);
    InputView bgView = InputView::from(bgResult, unionMinX, unionMinY);
    InputView maskInputView = InputView::from(maskResult, unionMinX, unionMinY);

    applyMatteComposite(outputBuf, unionWidth, fgView, bgView, maskInputView);

    return RenderResponse(std::move(outputBuf), Point{unionMinX, unionMinY});
}

// ============================================================================
// MatteNode - ヘルパー関数実装
// ============================================================================

int MatteNode::scanMaskZeroRanges(const uint8_t* maskData, int maskWidth,
                                  int& outLeftSkip, int& outRightSkip) {
    // 左端からの0スキップ（4バイト単位、アライメント対応）
    int leftSkip = 0;
    {
        // Phase 1: アライメントまで1バイトずつ
        uintptr_t addr = reinterpret_cast<uintptr_t>(maskData);
        int misalign = static_cast<int>(addr & 3);
        if (misalign != 0) {
            int alignBytes = 4 - misalign;
            while (alignBytes > 0 && leftSkip < maskWidth && maskData[leftSkip] == 0) {
                ++leftSkip;
                --alignBytes;
            }
            if (leftSkip < maskWidth && maskData[leftSkip] != 0) {
                outLeftSkip = leftSkip;
                outRightSkip = 0;
                // 右スキップも計算して返す
                goto scan_right;
            }
        }

        // Phase 2: 4バイト単位
        {
            auto plimit = (maskWidth - leftSkip) >> 2;
            if (plimit) {
                const uint32_t* p32 = reinterpret_cast<const uint32_t*>(maskData + leftSkip);
                while (plimit > 0 && *p32 == 0) {
                    ++p32;
                    leftSkip += 4;
                    --plimit;
                }
            }
        }

        // Phase 3: 残りを1バイトずつ
        while (leftSkip < maskWidth && maskData[leftSkip] == 0) {
            ++leftSkip;
        }
    }

    // 全面0なら終了
    if (leftSkip >= maskWidth) {
        outLeftSkip = maskWidth;
        outRightSkip = 0;
        return 0;
    }

scan_right:
    outLeftSkip = leftSkip;

    // 右端からの0スキップ（4バイト単位、アライメント対応）
    int rightSkip = 0;
    {
        const int limit = maskWidth - leftSkip;

        // Phase 1: アライメントまで1バイトずつ
        uintptr_t endAddr = reinterpret_cast<uintptr_t>(maskData + maskWidth);
        int misalign = static_cast<int>(endAddr & 3);
        if (misalign != 0) {
            while (misalign > 0 && rightSkip < limit && maskData[maskWidth - 1 - rightSkip] == 0) {
                ++rightSkip;
                --misalign;
            }
            if (rightSkip < limit && maskData[maskWidth - 1 - rightSkip] != 0) {
                outRightSkip = rightSkip;
                return maskWidth - leftSkip - rightSkip;
            }
        }

        // Phase 2: 4バイト単位
        {
            auto plimit = (limit - rightSkip) >> 2;
            if (plimit) {
                const uint32_t* p32 = reinterpret_cast<const uint32_t*>(maskData + maskWidth - rightSkip) - 1;
                while (plimit > 0 && *p32 == 0) {
                    --p32;
                    rightSkip += 4;
                    --plimit;
                }
            }
        }

        // Phase 3: 残りを1バイトずつ
        while (rightSkip < limit && maskData[maskWidth - 1 - rightSkip] == 0) {
            ++rightSkip;
        }
    }

    outRightSkip = rightSkip;
    return maskWidth - leftSkip - rightSkip;
}

// ============================================================================
// MatteNode - 合成処理実装
// ============================================================================

void MatteNode::applyMatteComposite(ImageBuffer& output, int outWidth,
                                    const InputView& fg, const InputView& bg, const InputView& mask) {
    ViewPort outView = output.view();
    uint8_t* outRow = static_cast<uint8_t*>(outView.data);

    // マスクがない場合 → 全面背景
    const uint8_t* maskRowBase = mask.rowAt(0);
    if (!maskRowBase) {
        const uint8_t* bgRowBase = bg.rowAt(0);
        copyRowRegion(outRow, bgRowBase, bg.offsetX, bg.width, 0, outWidth);
        return;
    }

    // 各入力の行ポインタ
    const uint8_t* fgRowBase = fg.rowAt(0);
    const uint8_t* bgRowBase = bg.rowAt(0);

    // マスクの有効X範囲（出力座標系）
    const int maskXStart = std::max(0, mask.offsetX);
    const int maskXEnd = std::min(outWidth, mask.width + mask.offsetX);

    // マスク範囲外の左側 → 背景のみ
    if (maskXStart > 0) {
        copyRowRegion(outRow, bgRowBase, bg.offsetX, bg.width, 0, maskXStart);
    }

    // マスクポインタ設定
    const uint8_t* maskP = maskRowBase + (maskXStart - mask.offsetX);
    const uint8_t* const maskPEnd = maskRowBase + (maskXEnd - mask.offsetX);
    int x = maskXStart;

    // ランレングス処理
    while (maskP < maskPEnd) {
        const uint8_t runAlpha = *maskP;
        const int runStart = x;

        // 同じalpha値の連続を検出
        do { ++maskP; ++x; } while (maskP < maskPEnd && *maskP == runAlpha);

        const int runEnd = x;

        if (runAlpha == 0) {
            copyRowRegion(outRow, bgRowBase, bg.offsetX, bg.width, runStart, runEnd);
        } else if (runAlpha == 255) {
            copyRowRegion(outRow, fgRowBase, fg.offsetX, fg.width, runStart, runEnd);
        } else {
            blendPixels(outRow, runStart, runEnd, runAlpha,
                        fgRowBase, fg.offsetX, fg.width,
                        bgRowBase, bg.offsetX, bg.width);
        }
    }

    // マスク範囲外の右側 → 背景のみ
    if (maskXEnd < outWidth) {
        copyRowRegion(outRow, bgRowBase, bg.offsetX, bg.width, maskXEnd, outWidth);
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

void MatteNode::blendPixels(uint8_t* outRow, int xStart, int xEnd, uint8_t alpha,
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

        outP[0] = static_cast<uint8_t>((fgR + bgR) / 255);
        outP[1] = static_cast<uint8_t>((fgG + bgG) / 255);
        outP[2] = static_cast<uint8_t>((fgB + bgB) / 255);
        outP[3] = static_cast<uint8_t>((fgA + bgA) / 255);

        outP += 4;
        if (fgP) fgP += 4;
        if (bgP) bgP += 4;
    }
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_MATTE_NODE_H
