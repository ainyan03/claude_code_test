#ifndef FLEXIMG_NODE_H
#define FLEXIMG_NODE_H

#include <vector>
#include "common.h"
#include "port.h"

namespace FLEXIMG_NAMESPACE {

// 前方宣言
struct RenderRequest;
struct RenderContext;
struct RenderResult;

// ========================================================================
// Node - ノード基底クラス
// ========================================================================
//
// パイプラインを構成するノードの基底クラスです。
// - 入力/出力ポートを持つ
// - 接続APIを提供（詳細API、簡易API、演算子）
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
    // 評価インターフェース（派生クラスで実装）
    // ========================================

    // 準備フェーズ（タイル処理前に1回呼ばれる）
    virtual void prepare(const RenderContext& ctx) { (void)ctx; }

    // ノード名（デバッグ用）
    virtual const char* name() const { return "Node"; }

    // 上流ノードを取得（入力ポート経由）
    Node* upstreamNode(int inputIndex = 0) const {
        if (inputIndex < 0 || inputIndex >= static_cast<int>(inputs_.size())) {
            return nullptr;
        }
        return inputs_[inputIndex].connectedNode();
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
