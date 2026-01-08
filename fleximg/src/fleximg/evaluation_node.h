#ifndef FLEXIMG_EVALUATION_NODE_H
#define FLEXIMG_EVALUATION_NODE_H

#include "common.h"
#include "viewport.h"
#include "image_buffer.h"
#include "eval_result.h"
#include "node_graph.h"
#include "operators.h"
#include <vector>
#include <memory>
#include <string>
#include <map>

namespace FLEXIMG_NAMESPACE {

// 前方宣言
class EvaluationNode;

// ========================================================================
// EvaluationNode - 評価ノード基底クラス
// ========================================================================

class EvaluationNode {
public:
    virtual ~EvaluationNode() = default;

    // メイン評価メソッド（派生クラスで実装）
    // 要求に基づいて上流を評価し、自身の処理を適用して結果を返す
    virtual EvalResult evaluate(const RenderRequest& request,
                                const RenderContext& context) = 0;

    // 入力要求の計算（派生クラスで実装）
    // 出力要求から、上流ノードに伝播する入力要求を計算
    virtual RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const = 0;

    // 描画準備（オプション、派生クラスでオーバーライド可能）
    // 逆行列計算やカーネル準備など、タイル処理前に1回だけ実行
    virtual void prepare(const RenderContext& context) { (void)context; }

    // ノードID（デバッグ用）
    std::string id;

    // 上流ノードへのポインタ（パイプライン構築時に設定）
    std::vector<EvaluationNode*> inputs;

protected:
    bool prepared_ = false;
};

// ========================================================================
// ImageEvalNode - 画像ノード（終端）
// ========================================================================

class ImageEvalNode : public EvaluationNode {
public:
    EvalResult evaluate(const RenderRequest& request,
                        const RenderContext& context) override;

    RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const override;

    // 画像データ（imageLibrary内のViewPortをコピー保持）
    ViewPort imageData;

    // 画像内の原点位置（0.0〜1.0、デフォルトは中央）
    float srcOriginX = 0.5f;
    float srcOriginY = 0.5f;
};

// ========================================================================
// FilterEvalNode - フィルタノード
// ========================================================================

class FilterEvalNode : public EvaluationNode {
public:
    EvalResult evaluate(const RenderRequest& request,
                        const RenderContext& context) override;

    RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const override;

    void prepare(const RenderContext& context) override;

    // フィルタオペレーター
    std::unique_ptr<NodeOperator> op;

    // フィルタ種別とパラメータ（prepare用）
    std::string filterType;
    std::vector<float> filterParams;
};

// ========================================================================
// AffineEvalNode - アフィン変換ノード
// ========================================================================

class AffineEvalNode : public EvaluationNode {
public:
    EvalResult evaluate(const RenderRequest& request,
                        const RenderContext& context) override;

    RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const override;

    void prepare(const RenderContext& context) override;

    // 変換行列
    AffineMatrix matrix;

    // 事前計算済み逆行列（固定小数点）
    int32_t fixedInvA = 0, fixedInvB = 0, fixedInvC = 0, fixedInvD = 0;
    int32_t fixedInvTx = 0, fixedInvTy = 0;
};

// ========================================================================
// CompositeEvalNode - 合成ノード
// ========================================================================

class CompositeEvalNode : public EvaluationNode {
public:
    EvalResult evaluate(const RenderRequest& request,
                        const RenderContext& context) override;

    RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const override;
};

// ========================================================================
// OutputEvalNode - 出力ノード（パイプラインの終端）
// ========================================================================

class OutputEvalNode : public EvaluationNode {
public:
    EvalResult evaluate(const RenderRequest& request,
                        const RenderContext& context) override;

    RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const override;

    // 出力先ViewPort（imageLibrary内のViewPortをコピー保持）
    ViewPort outputTarget;
};

// ========================================================================
// Pipeline - パイプライン構造体（全ノードの所有権を保持）
// ========================================================================

struct Pipeline {
    // 全ノードの所有権を保持
    std::vector<std::unique_ptr<EvaluationNode>> nodes;

    // 出力ノードへのポインタ（nodesの要素を指す）
    EvaluationNode* outputNode = nullptr;

    // 有効かどうか
    bool isValid() const { return outputNode != nullptr; }

    // 描画準備を実行
    void prepare(const RenderContext& context) {
        for (auto& node : nodes) {
            node->prepare(context);
        }
    }
};

// ========================================================================
// PipelineBuilder - パイプライン構築ユーティリティ
// ========================================================================

class PipelineBuilder {
public:
    // GraphNode/GraphConnection からパイプラインを構築
    static Pipeline build(
        const std::vector<GraphNode>& nodes,
        const std::vector<GraphConnection>& connections,
        const std::map<int, ViewPort>& imageLibrary);

private:
    // ノードタイプに応じたEvaluationNodeを生成
    // viewPort: image/outputノード用の画像データ（それ以外はnullptr）
    static std::unique_ptr<EvaluationNode> createEvalNode(
        const GraphNode& node,
        const ViewPort* viewPort);
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_EVALUATION_NODE_H
