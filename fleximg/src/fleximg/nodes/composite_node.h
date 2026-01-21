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
    bool onPullPrepare(const PrepareRequest& request) override {
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

    // onPullFinalize: 全上流ノードに終了を伝播
    void onPullFinalize() override {
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
    RenderResult onPullProcess(const RenderRequest& request) override {
        int numInputs = inputCount();
        if (numInputs == 0) return RenderResult();

        // バッファ内基準点位置（固定小数点 Q16.16）
        int_fixed canvasOriginX = request.origin.x;
        int_fixed canvasOriginY = request.origin.y;

        // Premul形式のキャンバスを作成（透明で初期化）
        ImageBuffer canvasBuf = canvas_utils::createPremulCanvas(request.width, request.height, InitPolicy::Zero);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        PerfMetrics::instance().nodes[NodeType::Composite].recordAlloc(
            canvasBuf.totalBytes(), canvasBuf.width(), canvasBuf.height());
#endif
        ViewPort canvasView = canvasBuf.view();
        bool hasContent = false;

        // under合成: 入力を順に評価して合成
        // 入力ポート0が最前面、以降が背面
        for (int i = 0; i < numInputs; i++) {
            Node* upstream = upstreamNode(i);
            if (!upstream) continue;

            // 上流を評価（計測対象外）
            RenderResult inputResult = upstream->pullProcess(request);

            // 空入力はスキップ
            if (!inputResult.isValid()) continue;

            hasContent = true;

            // ここからCompositeNode自身の処理を計測
            FLEXIMG_METRICS_SCOPE(NodeType::Composite);

            // フォーマット変換: blendUnderPremul関数がないフォーマットはRGBA8_Straightに変換
            PixelFormatID inputFmt = inputResult.view().formatID;
            if (!inputFmt->blendUnderPremul) {
                inputResult = canvas_utils::ensureBlendableFormat(std::move(inputResult));
            }

            // under合成: dstが不透明なら何もしない最適化が自動適用
            canvas_utils::placeUnder(canvasView, canvasOriginX, canvasOriginY,
                                     inputResult.view(), inputResult.origin.x, inputResult.origin.y);
        }

        // 全ての入力が空だった場合
        if (!hasContent) {
            return RenderResult(ImageBuffer(), Point{canvasOriginX, canvasOriginY});
        }

        // Premul → RGBA8_Straight に変換して返す
        ImageBuffer finalBuf = canvas_utils::finalizePremulCanvas(std::move(canvasBuf));
        return RenderResult(std::move(finalBuf), Point{canvasOriginX, canvasOriginY});
    }

protected:
    int nodeTypeForMetrics() const override { return NodeType::Composite; }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_COMPOSITE_NODE_H
