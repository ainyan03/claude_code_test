#ifndef FLEXIMG_AFFINE_NODE_H
#define FLEXIMG_AFFINE_NODE_H

#include "../core/node.h"
#include "../core/affine_capability.h"
#include "../core/perf_metrics.h"

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
// セッターはAffineCapability Mixinから継承:
// - setMatrix(), matrix()
// - setRotation(), setScale(), setTranslation(), setRotationScale()
//
// 使用例:
//   AffineNode affine;
//   affine.setRotation(0.5f);
//   src >> affine >> sink;
//

class AffineNode : public Node, public AffineCapability {
public:
    AffineNode() {
        initPorts(1, 1);  // 入力1、出力1
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "AffineNode"; }

protected:
    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: アフィン行列を上流に伝播し、SourceNodeで一括実行
    PrepareResponse onPullPrepare(const PrepareRequest& request) override;

    // onPushPrepare: アフィン行列を下流に伝播し、SinkNodeで一括実行
    PrepareResponse onPushPrepare(const PrepareRequest& request) override;

    // onPullProcess: AffineNodeは行列を保持するのみ、パススルー
    RenderResponse onPullProcess(const RenderRequest& request) override;

    // onPushProcess: AffineNodeは行列を保持するのみ、パススルー
    void onPushProcess(RenderResponse&& input,
                       const RenderRequest& request) override;


    int nodeTypeForMetrics() const override { return NodeType::Affine; }
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// AffineNode - Template Method フック実装
// ============================================================================

// 複数のAffineNodeがある場合は行列を合成する
PrepareResponse AffineNode::onPullPrepare(const PrepareRequest& request) {
    // 上流に渡すためのコピーを作成し、自身の行列を累積
    PrepareRequest upstreamRequest = request;
    if (upstreamRequest.hasAffine) {
        // 既存の行列（下流側）に自身の行列（上流側）を後から掛ける
        upstreamRequest.affineMatrix = upstreamRequest.affineMatrix * localMatrix_;
    } else {
        upstreamRequest.affineMatrix = localMatrix_;
        upstreamRequest.hasAffine = true;
    }

    // 上流へ伝播
    Node* upstream = upstreamNode(0);
    if (upstream) {
        return upstream->pullPrepare(upstreamRequest);  // パススルー
    }
    // 上流なし: 有効なデータがないのでサイズ0を返す
    PrepareResponse result;
    result.status = PrepareStatus::Prepared;
    // width/height/originはデフォルト値（0）のまま
    return result;
}

// 複数のAffineNodeがある場合は行列を合成する
PrepareResponse AffineNode::onPushPrepare(const PrepareRequest& request) {
    // 下流に渡すためのコピーを作成し、自身の行列を累積
    PrepareRequest downstreamRequest = request;
    if (downstreamRequest.hasPushAffine) {
        // 既存の行列（上流側）に自身の行列（下流側）を後から掛ける
        downstreamRequest.pushAffineMatrix = downstreamRequest.pushAffineMatrix * localMatrix_;
    } else {
        downstreamRequest.pushAffineMatrix = localMatrix_;
        downstreamRequest.hasPushAffine = true;
    }

    // 下流へ伝播
    Node* downstream = downstreamNode(0);
    if (downstream) {
        return downstream->pushPrepare(downstreamRequest);  // パススルー
    }
    // 下流なし: 有効なデータがないのでサイズ0を返す
    PrepareResponse result;
    result.status = PrepareStatus::Prepared;
    // width/height/originはデフォルト値（0）のまま
    return result;
}

RenderResponse AffineNode::onPullProcess(const RenderRequest& request) {
    Node* upstream = upstreamNode(0);
    if (!upstream) return RenderResponse();
    return upstream->pullProcess(request);
}

void AffineNode::onPushProcess(RenderResponse&& input,
                               const RenderRequest& request) {
    Node* downstream = downstreamNode(0);
    if (downstream) {
        downstream->pushProcess(std::move(input), request);
    }
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_AFFINE_NODE_H
