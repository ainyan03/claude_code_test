#ifndef FLEXIMG_PORT_H
#define FLEXIMG_PORT_H

#include "common.h"

namespace FLEXIMG_NAMESPACE {
namespace core {

// 前方宣言
class Node;

// ========================================================================
// Port - 接続点（1:1接続）
// ========================================================================
//
// ノード間の接続点を表します。
// - 各ポートは最大1つの相手ポートと接続可能
// - 接続は相互参照（双方向）
//

struct Port {
    Node* owner = nullptr;      // このポートを所有するノード
    Port* connected = nullptr;  // 接続先ポート（nullptr = 未接続）
    int index = 0;              // ノード内でのポート番号

    Port() = default;
    Port(Node* own, int idx) : owner(own), index(idx) {}

    // 接続状態
    bool isConnected() const { return connected != nullptr; }

    // 接続（相互参照を設定）
    // 戻り値: 成功=true, 既に接続済み=false
    bool connect(Port& other) {
        if (connected || other.connected) {
            return false;  // どちらかが既に接続済み
        }
        connected = &other;
        other.connected = this;
        return true;
    }

    // 切断
    void disconnect() {
        if (connected) {
            connected->connected = nullptr;
            connected = nullptr;
        }
    }

    // 接続先ノードを取得
    Node* connectedNode() const {
        return connected ? connected->owner : nullptr;
    }
};

} // namespace core

// [DEPRECATED] 後方互換性のため親名前空間に公開。将来廃止予定。
// 新規コードでは core:: プレフィックスを使用してください。
using core::Port;

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_PORT_H
