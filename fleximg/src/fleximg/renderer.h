#ifndef FLEXIMG_RENDERER_H
#define FLEXIMG_RENDERER_H

#include "common.h"
#include "render_types.h"
#include "nodes/sink_node.h"
#include "nodes/source_node.h"
#include "nodes/transform_node.h"
#include "nodes/filter_node.h"
#include "nodes/composite_node.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// Renderer - パイプライン実行者
// ========================================================================
//
// SinkNodeを起点としてパイプラインを実行します。
// - exec(): 簡易API（prepare → execute → finalize）
// - prepare()/execute()/finalize(): 詳細API
// - カスタムタイル戦略は派生クラスでオーバーライド
//

class Renderer {
public:
    explicit Renderer(SinkNode& output)
        : output_(&output) {}

    virtual ~Renderer() = default;

    // ========================================
    // 設定API
    // ========================================

    void setTileConfig(const TileConfig& config) {
        tileConfig_ = config;
    }

    void setDebugCheckerboard(bool enabled) {
        debugCheckerboard_ = enabled;
    }

    // パフォーマンス計測結果を取得
    const PerfMetrics& getPerfMetrics() const {
        return PerfMetrics::instance();
    }

    void resetPerfMetrics() {
        PerfMetrics::instance().reset();
    }

    // ========================================
    // 簡易API
    // ========================================

    void exec() {
        prepare();
        execute();
        finalize();
    }

    // ========================================
    // 詳細API
    // ========================================

    virtual void prepare();
    virtual void execute();
    virtual void finalize();

protected:
    SinkNode* output_;
    TileConfig tileConfig_;
    RenderContext context_;
    bool debugCheckerboard_ = false;

    // タイル処理
    virtual void processTile(int tileX, int tileY);

    // 上流ノードから画像を取得（再帰的に評価）
    RenderResult evaluateUpstream(Node* node, const RenderRequest& request);

    // ノードタイプ別の評価
    RenderResult evaluateSourceNode(SourceNode* src, const RenderRequest& request);
    RenderResult evaluateTransformNode(TransformNode* xform, const RenderRequest& request);
    RenderResult evaluateFilterNode(FilterNode* filter, const RenderRequest& request);
    RenderResult evaluateCompositeNode(CompositeNode* composite, const RenderRequest& request);
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_RENDERER_H
