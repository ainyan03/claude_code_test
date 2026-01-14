#ifndef FLEXIMG_COMPOSITE_NODE_H
#define FLEXIMG_COMPOSITE_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../image/pixel_format.h"
#include "../operations/blend.h"
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

    // 全上流ノードにPrepareRequestを伝播（循環検出+アフィン伝播）
    bool pullPrepare(const PrepareRequest& request) override {
        // 循環参照検出: Preparing状態で再訪問 = 循環
        if (pullPrepareState_ == PrepareState::Preparing) {
            pullPrepareState_ = PrepareState::CycleError;
            return false;
        }
        // DAG共有ノード: スキップ
        if (pullPrepareState_ == PrepareState::Prepared) {
            return true;
        }
        // 既にエラー状態
        if (pullPrepareState_ == PrepareState::CycleError) {
            return false;
        }

        pullPrepareState_ = PrepareState::Preparing;

        // 全上流へ伝播
        int numInputs = inputCount();
        for (int i = 0; i < numInputs; ++i) {
            Node* upstream = upstreamNode(i);
            if (upstream) {
                // 各上流に同じリクエストを伝播
                // 注意: アフィン行列は共有されるため、各上流で同じ変換が適用される
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
        // 既にIdleなら何もしない（循環防止）
        if (pullPrepareState_ == PrepareState::Idle) {
            return;
        }
        // 状態リセット
        pullPrepareState_ = PrepareState::Idle;

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
        // 循環エラー状態ならスキップ（無限再帰防止）
        if (pullPrepareState_ != PrepareState::Prepared) {
            return RenderResult();
        }
        int numInputs = inputCount();
        if (numInputs == 0) return RenderResult();

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        uint32_t compositeTime = 0;
        int compositeCount = 0;
#endif

        RenderResult canvas;
        bool canvasInitialized = false;
        // バッファ内基準点位置（固定小数点 Q24.8）
        int_fixed8 canvasOriginX = request.origin.x;
        int_fixed8 canvasOriginY = request.origin.y;

        // 逐次合成: 入力を1つずつ評価して合成
        for (int i = 0; i < numInputs; i++) {
            Node* upstream = upstreamNode(i);
            if (!upstream) continue;

            // 上流を評価（新APIを使用）
            RenderResult inputResult = upstream->pullProcess(request);

            // 空入力はスキップ
            if (!inputResult.isValid()) continue;

            // フォーマット変換: blend関数が対応していないフォーマットは変換
            // 対応フォーマット: RGBA8_Straight, RGBA16_Premultiplied
            PixelFormatID inputFmt = inputResult.view().formatID;
            if (inputFmt != PixelFormatIDs::RGBA8_Straight &&
                inputFmt != PixelFormatIDs::RGBA16_Premultiplied) {
                // RGBA16_Premultiplied に変換（合成キャンバスと同じフォーマット）
                Point savedOrigin = inputResult.origin;
                inputResult = RenderResult(
                    std::move(inputResult.buffer).toFormat(PixelFormatIDs::RGBA16_Premultiplied),
                    savedOrigin
                );
            }

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

                blend::first(canvasView, request.origin.x, request.origin.y,
                            inputView, inputResult.origin.x, inputResult.origin.y);

                canvas = RenderResult(std::move(canvasBuf),
                                     Point{canvasOriginX, canvasOriginY});
                canvasInitialized = true;
            } else {
                // 2枚目以降 → ブレンド処理
                ViewPort canvasView = canvas.view();
                ViewPort inputView = inputResult.view();

                blend::onto(canvasView, canvas.origin.x, canvas.origin.y,
                           inputView, inputResult.origin.x, inputResult.origin.y);
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
            return RenderResult(ImageBuffer(), Point{canvasOriginX, canvasOriginY});
        }

        return canvas;
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_COMPOSITE_NODE_H
