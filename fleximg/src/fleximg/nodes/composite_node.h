#ifndef FLEXIMG_COMPOSITE_NODE_H
#define FLEXIMG_COMPOSITE_NODE_H

#include "../node.h"

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
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_COMPOSITE_NODE_H
