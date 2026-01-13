#ifndef FLEXIMG_NODE_H
#define FLEXIMG_NODE_H

#include <vector>
#include "common.h"
#include "port.h"
#include "perf_metrics.h"
#include "../image/render_types.h"
#include "../image/image_buffer.h"

namespace FLEXIMG_NAMESPACE {
namespace core {

// ========================================================================
// PrepareState - ノードの準備状態（循環参照検出用）
// ========================================================================

enum class PrepareState {
    Idle,       // 未処理（初期状態）
    Preparing,  // 処理中（この状態で再訪問 = 循環参照）
    Prepared,   // 処理完了（この状態で再訪問 = DAG共有、スキップ可）
    CycleError  // 循環参照エラー（processをスキップ）
};

// ========================================================================
// Node - ノード基底クラス
// ========================================================================
//
// パイプラインを構成するノードの基底クラスです。
// - 入力/出力ポートを持つ
// - 接続APIを提供（詳細API、簡易API、演算子）
// - プル型/プッシュ型の両インターフェースをサポート
//
// API:
// - pullProcess(): 上流から画像を取得して処理（プル型）
// - pushProcess(): 下流へ画像を渡す（プッシュ型）
// - process(): 共通処理（派生クラスで実装）
//

class Node {
public:
    virtual ~Node() = default;

    // ========================================
    // ポートアクセス（詳細API）
    // ========================================

    Port* inputPort(int index = 0) {
        return (index >= 0 && index < static_cast<int>(inputs_.size()))
            ? &inputs_[index] : nullptr;
    }

    Port* outputPort(int index = 0) {
        return (index >= 0 && index < static_cast<int>(outputs_.size()))
            ? &outputs_[index] : nullptr;
    }

    int inputPortCount() const { return static_cast<int>(inputs_.size()); }
    int outputPortCount() const { return static_cast<int>(outputs_.size()); }

    // ========================================
    // 接続API（簡易API）
    // ========================================

    // このノードの出力をtargetの入力に接続
    bool connectTo(Node& target, int targetInputIndex = 0, int outputIndex = 0) {
        Port* out = outputPort(outputIndex);
        Port* in = target.inputPort(targetInputIndex);
        return (out && in) ? out->connect(*in) : false;
    }

    // sourceの出力をこのノードの入力に接続
    bool connectFrom(Node& source, int sourceOutputIndex = 0, int inputIndex = 0) {
        return source.connectTo(*this, inputIndex, sourceOutputIndex);
    }

    // ========================================
    // 演算子（チェーン接続用）
    // ========================================

    // src >> affine >> sink のような記述を可能にする
    Node& operator>>(Node& downstream) {
        connectTo(downstream);
        return downstream;
    }

    Node& operator<<(Node& upstream) {
        connectFrom(upstream);
        return *this;
    }

    // ========================================
    // 新API: 共通処理（派生クラスで実装）
    // ========================================

    // 入力画像から出力画像を生成
    virtual RenderResult process(RenderResult&& input,
                                 const RenderRequest& request) {
        (void)request;
        return std::move(input);  // デフォルトはパススルー
    }

    // 準備処理（スクリーン情報を受け取る）
    virtual void prepare(const RenderRequest& screenInfo) {
        (void)screenInfo;
    }

    // 終了処理
    virtual void finalize() {
        // デフォルトは何もしない
    }

    // ========================================
    // 新API: プル型インターフェース（上流側）
    // ========================================

    // 上流から画像を取得して処理
    virtual RenderResult pullProcess(const RenderRequest& request) {
        // 循環エラー状態ならスキップ（無限再帰防止）
        if (pullPrepareState_ != PrepareState::Prepared) {
            return RenderResult();
        }
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();
        RenderResult input = upstream->pullProcess(request);
        return process(std::move(input), request);
    }

    // 上流へ準備を伝播（循環参照検出付き）
    // 戻り値: true = 成功、false = 循環参照検出
    virtual bool pullPrepare(const RenderRequest& screenInfo) {
        // 循環参照検出: Preparing状態で再訪問 = 循環
        if (pullPrepareState_ == PrepareState::Preparing) {
            pullPrepareState_ = PrepareState::CycleError;  // エラー状態を設定
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

        // 上流へ伝播
        Node* upstream = upstreamNode(0);
        if (upstream) {
            if (!upstream->pullPrepare(screenInfo)) {
                pullPrepareState_ = PrepareState::CycleError;  // 上流エラーも伝播
                return false;
            }
        }

        prepare(screenInfo);
        pullPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // 上流へ終了を伝播
    virtual void pullFinalize() {
        // 既にIdleなら何もしない（循環防止）
        if (pullPrepareState_ == PrepareState::Idle) {
            return;
        }
        // 状態リセット
        pullPrepareState_ = PrepareState::Idle;

        finalize();
        Node* upstream = upstreamNode(0);
        if (upstream) {
            upstream->pullFinalize();
        }
    }

    // ========================================
    // 新API: プッシュ型インターフェース（下流側）
    // ========================================

    // 上流から画像を受け取って処理し、下流へ渡す
    virtual void pushProcess(RenderResult&& input,
                             const RenderRequest& request) {
        // 循環エラー状態ならスキップ（無限再帰防止）
        if (pushPrepareState_ != PrepareState::Prepared) {
            return;
        }
        RenderResult output = process(std::move(input), request);
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(std::move(output), request);
        }
    }

    // 下流へ準備を伝播（循環参照検出付き）
    // 戻り値: true = 成功、false = 循環参照検出
    virtual bool pushPrepare(const RenderRequest& screenInfo) {
        // 循環参照検出: Preparing状態で再訪問 = 循環
        if (pushPrepareState_ == PrepareState::Preparing) {
            pushPrepareState_ = PrepareState::CycleError;
            return false;
        }
        // DAG共有ノード: スキップ
        if (pushPrepareState_ == PrepareState::Prepared) {
            return true;
        }
        // 既にエラー状態
        if (pushPrepareState_ == PrepareState::CycleError) {
            return false;
        }

        pushPrepareState_ = PrepareState::Preparing;

        prepare(screenInfo);

        // 下流へ伝播
        Node* downstream = downstreamNode(0);
        if (downstream) {
            if (!downstream->pushPrepare(screenInfo)) {
                pushPrepareState_ = PrepareState::CycleError;
                return false;
            }
        }

        pushPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // 下流へ終了を伝播
    virtual void pushFinalize() {
        // 既にIdleなら何もしない（循環防止）
        if (pushPrepareState_ == PrepareState::Idle) {
            return;
        }
        // 状態リセット
        pushPrepareState_ = PrepareState::Idle;

        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushFinalize();
        }
        finalize();
    }

    // ノード名（デバッグ用）
    virtual const char* name() const { return "Node"; }

    // ========================================
    // メトリクス用ノードタイプ
    // ========================================

    // 派生クラスでオーバーライドしてNodeType::Xxxを返す
    virtual int nodeTypeForMetrics() const { return 0; }

    // ========================================
    // ノードアクセス
    // ========================================

    // 上流ノードを取得（入力ポート経由）
    Node* upstreamNode(int inputIndex = 0) const {
        if (inputIndex < 0 || inputIndex >= static_cast<int>(inputs_.size())) {
            return nullptr;
        }
        return inputs_[inputIndex].connectedNode();
    }

    // 下流ノードを取得（出力ポート経由）
    Node* downstreamNode(int outputIndex = 0) const {
        if (outputIndex < 0 || outputIndex >= static_cast<int>(outputs_.size())) {
            return nullptr;
        }
        return outputs_[outputIndex].connectedNode();
    }

protected:
    std::vector<Port> inputs_;
    std::vector<Port> outputs_;

    // 循環参照検出用状態
    PrepareState pullPrepareState_ = PrepareState::Idle;
    PrepareState pushPrepareState_ = PrepareState::Idle;

    // ========================================
    // ヘルパーメソッド
    // ========================================

    // フォーマット変換ヘルパー（メトリクス記録付き）
    // 参照モードから所有モードに変わった場合、ノード別統計に記録
    ImageBuffer convertFormat(ImageBuffer&& buffer, PixelFormatID target,
                              FormatConversion mode = FormatConversion::CopyIfNeeded) {
        bool wasOwning = buffer.ownsMemory();
        ImageBuffer result = std::move(buffer).toFormat(target, mode);

        // 参照→所有モードへの変換時にメトリクス記録
        if (!wasOwning && result.ownsMemory()) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
            PerfMetrics::instance().nodes[nodeTypeForMetrics()].recordAlloc(
                result.totalBytes(), result.width(), result.height());
#endif
        }
        return result;
    }

    // 派生クラス用：ポート初期化
    void initPorts(int inputCount, int outputCount) {
        inputs_.resize(inputCount);
        outputs_.resize(outputCount);
        for (int i = 0; i < inputCount; ++i) {
            inputs_[i] = Port(this, i);
        }
        for (int i = 0; i < outputCount; ++i) {
            outputs_[i] = Port(this, i);
        }
    }
};

} // namespace core

// [DEPRECATED] 後方互換性のため親名前空間に公開。将来廃止予定。
// 新規コードでは core:: プレフィックスを使用してください。
using core::PrepareState;
using core::Node;

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_NODE_H
