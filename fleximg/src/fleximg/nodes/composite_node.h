#ifndef FLEXIMG_COMPOSITE_NODE_H
#define FLEXIMG_COMPOSITE_NODE_H

#include "../node.h"
#include "../image_buffer.h"
#include "../operations/blend.h"
#include "../pixel_format.h"
#include "../perf_metrics.h"
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// CompositeNode - 合成ノード
// ========================================================================
//
// 複数の入力画像を合成して1つの出力を生成します。
// - 入力: コンストラクタで指定（デフォルト2）
// - 出力: 1ポート
//
// 合成順序:
// - 入力ポート0が最背面（最初に描画）
// - 入力ポート1以降が順に前面に合成
//
// 使用例:
//   CompositeNode composite(3);  // 3入力
//   bg >> composite;             // ポート0（背景）
//   fg1.connectTo(composite, 1); // ポート1（前景1）
//   fg2.connectTo(composite, 2); // ポート2（前景2）
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
        inputs_.resize(count);
        for (int i = 0; i < count; ++i) {
            if (inputs_[i].owner == nullptr) {
                inputs_[i] = Port(this, i);
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
    // プル型インターフェース
    // ========================================

    // 全上流ノードに準備を伝播
    void pullPrepare(const RenderRequest& screenInfo) override {
        int numInputs = inputCount();
        for (int i = 0; i < numInputs; ++i) {
            Node* upstream = upstreamNode(i);
            if (upstream) {
                upstream->pullPrepare(screenInfo);
            }
        }
        prepare(screenInfo);
    }

    // 全上流ノードに終了を伝播
    void pullFinalize() override {
        finalize();
        int numInputs = inputCount();
        for (int i = 0; i < numInputs; ++i) {
            Node* upstream = upstreamNode(i);
            if (upstream) {
                upstream->pullFinalize();
            }
        }
    }

    // 複数の上流から画像を取得して合成するため、pullProcess()を直接オーバーライド
    RenderResult pullProcess(const RenderRequest& request) override {
        int numInputs = inputCount();
        if (numInputs == 0) return RenderResult();

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        uint32_t compositeTime = 0;
        int compositeCount = 0;
#endif

        RenderResult canvas;
        bool canvasInitialized = false;
        float canvasOriginX = -request.originX;
        float canvasOriginY = -request.originY;

        // 逐次合成: 入力を1つずつ評価して合成
        for (int i = 0; i < numInputs; i++) {
            Node* upstream = upstreamNode(i);
            if (!upstream) continue;

            // 上流を評価（新APIを使用）
            RenderResult inputResult = upstream->pullProcess(request);

            // 空入力はスキップ
            if (!inputResult.isValid()) continue;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
            auto compStart = std::chrono::high_resolution_clock::now();
#endif

            if (!canvasInitialized) {
                // 最初の非空入力 → 新しいキャンバスを作成
                ImageBuffer canvasBuf(request.width, request.height,
                                      PixelFormatIDs::RGBA16_Premultiplied);
#ifdef FLEXIMG_DEBUG_PERF_METRICS
                PerfMetrics::instance().nodes[NodeType::Composite].recordAlloc(
                    canvasBuf.totalBytes(), canvasBuf.width(), canvasBuf.height());
#endif
                ViewPort canvasView = canvasBuf.view();
                ViewPort inputView = inputResult.view();

                blend::first(canvasView, request.originX, request.originY,
                            inputView, -inputResult.origin.x, -inputResult.origin.y);

                canvas = RenderResult(std::move(canvasBuf),
                                     Point2f(canvasOriginX, canvasOriginY));
                canvasInitialized = true;
            } else {
                // 2枚目以降 → ブレンド処理
                ViewPort canvasView = canvas.view();
                ViewPort inputView = inputResult.view();

                blend::onto(canvasView, -canvas.origin.x, -canvas.origin.y,
                           inputView, -inputResult.origin.x, -inputResult.origin.y);
            }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
            compositeTime += std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - compStart).count();
            compositeCount++;
#endif
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        if (compositeCount > 0) {
            auto& mComp = PerfMetrics::instance().nodes[NodeType::Composite];
            mComp.time_us += compositeTime;
            mComp.count += compositeCount;
        }
#endif

        // 全ての入力が空だった場合
        if (!canvasInitialized) {
            return RenderResult(ImageBuffer(), Point2f(canvasOriginX, canvasOriginY));
        }

        return canvas;
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_COMPOSITE_NODE_H
