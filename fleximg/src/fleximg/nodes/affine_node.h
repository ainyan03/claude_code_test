#ifndef FLEXIMG_AFFINE_NODE_H
#define FLEXIMG_AFFINE_NODE_H

#include "../core/node.h"
#include "../core/common.h"
#include "../core/perf_metrics.h"
#include <cassert>
#include <cmath>

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// AffineNode - アフィン変換ノード
// ========================================================================
//
// 入力画像に対してアフィン変換（回転・拡縮・平行移動）を適用します。
// - 入力: 1ポート
// - 出力: 1ポート
//
// 特徴:
// - アフィン行列を保持し、SourceNode/SinkNodeに伝播する
// - 実際のDDA処理はSourceNode（プル型）またはSinkNode（プッシュ型）で実行
// - 複数のAffineNodeがある場合は行列を合成して伝播
//
// 使用例:
//   AffineNode affine;
//   affine.setMatrix(AffineMatrix::rotation(0.5f));
//   src >> affine >> sink;
//

class AffineNode : public Node {
public:
    AffineNode() {
        initPorts(1, 1);  // 入力1、出力1
    }

    // ========================================
    // 変換設定
    // ========================================

    void setMatrix(const AffineMatrix& m) { matrix_ = m; }
    const AffineMatrix& matrix() const { return matrix_; }

    // 便利なセッター（各セッターは担当要素のみを変更し、他の要素は維持）

    // 回転を設定（a,b,c,dのみ変更、tx,tyは維持）
    void setRotation(float radians) {
        float c = std::cosf(radians);
        float s = std::sinf(radians);
        matrix_.a = c;  matrix_.b = -s;
        matrix_.c = s;  matrix_.d = c;
    }

    // スケールを設定（a,b,c,dのみ変更、tx,tyは維持）
    void setScale(float sx, float sy) {
        matrix_.a = sx; matrix_.b = 0;
        matrix_.c = 0;  matrix_.d = sy;
    }

    // 平行移動を設定（tx,tyのみ変更、a,b,c,dは維持）
    void setTranslation(float tx, float ty) {
        matrix_.tx = tx; matrix_.ty = ty;
    }

    // 回転+スケールを設定（a,b,c,dのみ変更、tx,tyは維持）
    void setRotationScale(float radians, float sx, float sy) {
        float c = std::cosf(radians);
        float s = std::sinf(radians);
        matrix_.a = c * sx;  matrix_.b = -s * sy;
        matrix_.c = s * sx;  matrix_.d = c * sy;
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "AffineNode"; }

    // ========================================
    // プル型準備（行列伝播）
    // ========================================
    //
    // アフィン行列を上流に伝播し、SourceNodeで一括実行する。
    // 複数のAffineNodeがある場合は行列を合成する。
    //

    bool pullPrepare(const PrepareRequest& request) override {
        bool shouldContinue;
        if (!checkPrepareState(pullPrepareState_, shouldContinue)) {
            return false;
        }
        if (!shouldContinue) {
            return true;  // DAG共有ノード: スキップ
        }

        // 上流に渡すためのコピーを作成し、自身の行列を累積
        PrepareRequest upstreamRequest = request;
        if (upstreamRequest.hasAffine) {
            // 既存の行列（下流側）に自身の行列（上流側）を後から掛ける
            upstreamRequest.affineMatrix = upstreamRequest.affineMatrix * matrix_;
        } else {
            upstreamRequest.affineMatrix = matrix_;
            upstreamRequest.hasAffine = true;
        }

        // 上流へ伝播
        Node* upstream = upstreamNode(0);
        if (upstream) {
            if (!upstream->pullPrepare(upstreamRequest)) {
                pullPrepareState_ = PrepareState::CycleError;
                return false;
            }
        }

        pullPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // ========================================
    // プッシュ型準備（行列伝播）
    // ========================================
    //
    // アフィン行列を下流に伝播し、SinkNodeで一括実行する。
    // 複数のAffineNodeがある場合は行列を合成する。
    //

    bool pushPrepare(const PrepareRequest& request) override {
        bool shouldContinue;
        if (!checkPrepareState(pushPrepareState_, shouldContinue)) {
            return false;
        }
        if (!shouldContinue) {
            return true;  // DAG共有ノード: スキップ
        }

        // 下流に渡すためのコピーを作成し、自身の行列を累積
        PrepareRequest downstreamRequest = request;
        if (downstreamRequest.hasPushAffine) {
            // 既存の行列（上流側）に自身の行列（下流側）を後から掛ける
            downstreamRequest.pushAffineMatrix = downstreamRequest.pushAffineMatrix * matrix_;
        } else {
            downstreamRequest.pushAffineMatrix = matrix_;
            downstreamRequest.hasPushAffine = true;
        }

        // 下流へ伝播
        Node* downstream = downstreamNode(0);
        if (downstream) {
            if (!downstream->pushPrepare(downstreamRequest)) {
                pushPrepareState_ = PrepareState::CycleError;
                return false;
            }
        }

        pushPrepareState_ = PrepareState::Prepared;
        return true;
    }

    // ========================================
    // プル型インターフェース
    // ========================================

    RenderResult pullProcess(const RenderRequest& request) override {
        assert(request.height == 1 && "Scanline processing requires height == 1");
        if (pullPrepareState_ != PrepareState::Prepared) {
            return RenderResult();
        }
        Node* upstream = upstreamNode(0);
        if (!upstream) return RenderResult();
        return upstream->pullProcess(request);
    }

    // ========================================
    // プッシュ型インターフェース
    // ========================================

    void pushProcess(RenderResult&& input,
                     const RenderRequest& request) override {
        if (pushPrepareState_ != PrepareState::Prepared) {
            return;
        }
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushProcess(std::move(input), request);
        }
    }

protected:
    int nodeTypeForMetrics() const override { return NodeType::Affine; }

private:
    AffineMatrix matrix_;  // 恒等行列がデフォルト
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_AFFINE_NODE_H
