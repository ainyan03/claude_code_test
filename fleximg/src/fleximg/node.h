#ifndef FLEXIMG_NODE_H
#define FLEXIMG_NODE_H

#include <vector>
#include "common.h"
#include "port.h"
#include "render_types.h"

namespace FLEXIMG_NAMESPACE {

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
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();
        RenderResult input = upstream->pullProcess(request);
        return process(std::move(input), request);
    }

    // 上流へ準備を伝播
    virtual void pullPrepare(const RenderRequest& screenInfo) {
        Node* upstream = upstreamNode(0);
        if (upstream) {
            upstream->pullPrepare(screenInfo);
        }
        prepare(screenInfo);
    }

    // 上流へ終了を伝播
    virtual void pullFinalize() {
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
        RenderResult output = process(std::move(input), request);
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(std::move(output), request);
        }
    }

    // 下流へ準備を伝播
    virtual void pushPrepare(const RenderRequest& screenInfo) {
        prepare(screenInfo);
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushPrepare(screenInfo);
        }
    }

    // 下流へ終了を伝播
    virtual void pushFinalize() {
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushFinalize();
        }
        finalize();
    }

    // ノード名（デバッグ用）
    virtual const char* name() const { return "Node"; }

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

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_NODE_H
