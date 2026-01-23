#ifndef FLEXIMG_COMPOSITE_NODE_H
#define FLEXIMG_COMPOSITE_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../image/pixel_format.h"
#include "../operations/canvas_utils.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// CompositeNode - 合成ノード
// ========================================================================
//
// 複数の入力画像を合成して1つの出力を生成します。
// - 入力: コンストラクタで指定（デフォルト2）
// - 出力: 1ポート
//
// 合成方式:
// - デフォルト: 8bit Straight形式（省メモリ、4バイト/ピクセル）
// - FLEXIMG_ENABLE_PREMUL定義時: 16bit Premultiplied形式（高精度、8バイト/ピクセル）
//
// 合成順序（under合成）:
// - 入力ポート0が最前面（最初に描画）
// - 入力ポート1以降が順に背面に合成
// - 既に不透明なピクセルは後のレイヤー処理をスキップ
//
// 使用例:
//   CompositeNode composite(3);  // 3入力
//   fg >> composite;             // ポート0（最前面）
//   mid.connectTo(composite, 1); // ポート1（中間）
//   bg.connectTo(composite, 2);  // ポート2（最背面）
//   composite >> sink;
//

class CompositeNode : public Node {
public:
    explicit CompositeNode(int inputCount = 2) {
        initPorts(inputCount, 1);  // 入力N、出力1
    }

    // ========================================
    // 入力管理
    // ========================================

    // 入力数を変更（既存接続は維持）
    void setInputCount(int count) {
        if (count < 1) count = 1;
        inputs_.resize(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            if (inputs_[static_cast<size_t>(i)].owner == nullptr) {
                inputs_[static_cast<size_t>(i)] = Port(this, i);
            }
        }
    }

    int inputCount() const {
        return static_cast<int>(inputs_.size());
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "CompositeNode"; }

    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: 全上流ノードにPrepareRequestを伝播
    bool onPullPrepare(const PrepareRequest& request) override;

    // onPullFinalize: 全上流ノードに終了を伝播
    void onPullFinalize() override;

    // onPullProcess: 複数の上流から画像を取得してunder合成
    RenderResult onPullProcess(const RenderRequest& request) override;

protected:
    int nodeTypeForMetrics() const override { return NodeType::Composite; }
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// CompositeNode - Template Method フック実装
// ============================================================================

bool CompositeNode::onPullPrepare(const PrepareRequest& request) {
    // 全上流へ伝播
    int numInputs = inputCount();
    for (int i = 0; i < numInputs; ++i) {
        Node* upstream = upstreamNode(i);
        if (upstream) {
            // 各上流に同じリクエストを伝播
            // 注意: アフィン行列は共有されるため、各上流で同じ変換が適用される
            if (!upstream->pullPrepare(request)) {
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

    return true;
}

void CompositeNode::onPullFinalize() {
    finalize();
    int numInputs = inputCount();
    for (int i = 0; i < numInputs; ++i) {
        Node* upstream = upstreamNode(i);
        if (upstream) {
            upstream->pullFinalize();
        }
    }
}

// onPullProcess: 複数の上流から画像を取得してunder合成
// under合成: 手前から奥へ処理し、不透明な部分は後のレイヤーをスキップ
// 最適化: height=1 前提（パイプラインは常にスキャンライン単位で処理）
RenderResult CompositeNode::onPullProcess(const RenderRequest& request) {
    int numInputs = inputCount();
    if (numInputs == 0) return RenderResult();

    // バッファ内基準点位置（固定小数点 Q16.16）
    int_fixed canvasOriginX = request.origin.x;
    int_fixed canvasOriginY = request.origin.y;

    // キャンバスを作成（height=1、未初期化）
#ifdef FLEXIMG_ENABLE_PREMUL
    // 16bit Premultiplied形式: 8バイト/ピクセル（高精度）
    constexpr size_t bytesPerPixel = 8;
    PixelFormatID canvasFormat = PixelFormatIDs::RGBA16_Premultiplied;
#else
    // 8bit Straight形式: 4バイト/ピクセル（省メモリ）
    constexpr size_t bytesPerPixel = 4;
    PixelFormatID canvasFormat = PixelFormatIDs::RGBA8_Straight;
#endif
    ImageBuffer canvasBuf(request.width, 1, canvasFormat,
                          InitPolicy::Uninitialized, allocator());
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().nodes[NodeType::Composite].recordAlloc(
        canvasBuf.totalBytes(), canvasBuf.width(), canvasBuf.height());
#endif
    uint8_t* canvasRow = static_cast<uint8_t*>(canvasBuf.view().pixelAt(0, 0));

    // 有効範囲を追跡（下流へ必要な範囲のみを返すため）
    int validStartX = request.width;  // 左端（右端で初期化）
    int validEndX = 0;                // 右端（左端で初期化）

    // under合成: 入力を順に評価して合成
    // 入力ポート0が最前面、以降が背面
    bool isFirstContent = true;
    for (int i = 0; i < numInputs; i++) {
        Node* upstream = upstreamNode(i);
        if (!upstream) continue;

        // 上流を評価（計測対象外）
        RenderResult inputResult = upstream->pullProcess(request);

        // 空入力はスキップ
        if (!inputResult.isValid()) continue;

        // ここからCompositeNode自身の処理を計測
        FLEXIMG_METRICS_SCOPE(NodeType::Composite);

        // X方向オフセット計算（Y方向は不要、height=1前提）
        int offsetX = from_fixed(canvasOriginX - inputResult.origin.x);
        int srcStartX = std::max(0, -offsetX);
        int dstStartX = std::max(0, offsetX);
        int copyWidth = std::min(inputResult.view().width - srcStartX,
                                 request.width - dstStartX);
        if (copyWidth <= 0) continue;

        const void* srcRow = inputResult.view().pixelAt(srcStartX, 0);
        uint8_t* dstRow = canvasRow + static_cast<size_t>(dstStartX) * bytesPerPixel;
        PixelFormatID srcFmt = inputResult.view().formatID;

        // 今回の描画範囲
        int curEndX = dstStartX + copyWidth;

        if (isFirstContent) {
            // 初回: 変換コピーのみ（余白ゼロクリア不要、cropViewで切り捨て）
            FLEXIMG_NAMESPACE::convertFormat(srcRow, srcFmt, dstRow, canvasFormat, copyWidth);
            validStartX = dstStartX;
            validEndX = curEndX;
            isFirstContent = false;
        } else {
            // 2回目以降: 範囲拡張があれば拡張部分をゼロクリア
            if (dstStartX < validStartX) {
                std::memset(canvasRow + static_cast<size_t>(dstStartX) * bytesPerPixel, 0,
                            static_cast<size_t>(validStartX - dstStartX) * bytesPerPixel);
                validStartX = dstStartX;
            }
            if (curEndX > validEndX) {
                std::memset(canvasRow + static_cast<size_t>(validEndX) * bytesPerPixel, 0,
                            static_cast<size_t>(curEndX - validEndX) * bytesPerPixel);
                validEndX = curEndX;
            }

            // under合成
#ifdef FLEXIMG_ENABLE_PREMUL
            // 16bit Premultiplied形式でのunder合成
            if (srcFmt->blendUnderPremul) {
                srcFmt->blendUnderPremul(dstRow, srcRow, copyWidth, nullptr);
            } else if (srcFmt->toPremul) {
                // blendUnderPremulがない場合、一時バッファでPremul変換してからブレンド
                ImageBuffer tempBuf(copyWidth, 1, PixelFormatIDs::RGBA16_Premultiplied,
                                    InitPolicy::Uninitialized, allocator());
                srcFmt->toPremul(tempBuf.view().pixelAt(0, 0), srcRow, copyWidth, nullptr);
                PixelFormatIDs::RGBA16_Premultiplied->blendUnderPremul(
                    dstRow, tempBuf.view().pixelAt(0, 0), copyWidth, nullptr);
            }
#else
            // 8bit Straight形式でのunder合成
            if (srcFmt->blendUnderStraight) {
                srcFmt->blendUnderStraight(dstRow, srcRow, copyWidth, nullptr);
            } else if (srcFmt->toStraight) {
                // blendUnderStraightがない場合、一時バッファでStraight変換してからブレンド
                ImageBuffer tempBuf(copyWidth, 1, PixelFormatIDs::RGBA8_Straight,
                                    InitPolicy::Uninitialized, allocator());
                srcFmt->toStraight(tempBuf.view().pixelAt(0, 0), srcRow, copyWidth, nullptr);
                PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
                    dstRow, tempBuf.view().pixelAt(0, 0), copyWidth, nullptr);
            }
#endif
            // 対応関数がない場合はスキップ
        }
    }

    // 全ての入力が空だった場合
    if (validStartX >= validEndX) {
        return RenderResult(ImageBuffer(), Point{canvasOriginX, canvasOriginY});
    }

    // 余白をゼロクリアして全幅を返す
    // TODO: cropViewで有効範囲のみを返す最適化（要調査）
    if (validStartX > 0) {
        std::memset(canvasRow, 0, static_cast<size_t>(validStartX) * bytesPerPixel);
    }
    if (validEndX < request.width) {
        std::memset(canvasRow + static_cast<size_t>(validEndX) * bytesPerPixel, 0,
                    static_cast<size_t>(request.width - validEndX) * bytesPerPixel);
    }
    return RenderResult(std::move(canvasBuf), Point{canvasOriginX, canvasOriginY});
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_COMPOSITE_NODE_H
