#ifndef FLEXIMG_NODE_GRAPH_H
#define FLEXIMG_NODE_GRAPH_H

#include "common.h"
#include "viewport.h"
#include "image_buffer.h"
#include "image_types.h"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <memory>

// ========================================================================
// デバッグ機能制御マクロ
// FLEXIMG_DEBUG が定義されている場合のみ計測機能が有効になる
// ========================================================================
#ifdef FLEXIMG_DEBUG
#define FLEXIMG_DEBUG_PERF_METRICS 1
#endif

namespace FLEXIMG_NAMESPACE {

// 前方宣言
struct Pipeline;
class EvaluationNode;

// ========================================================================
// タイル分割評価システム用構造体
// ========================================================================

// 前方宣言
struct PerfMetrics;

// 出力全体情報（段階0で伝播）
struct RenderContext {
    int totalWidth = 0;
    int totalHeight = 0;
    float originX = 0;     // dstOrigin X
    float originY = 0;     // dstOrigin Y

    int tileWidth = 0;      // 0 = キャンバス幅（分割なし）
    int tileHeight = 0;     // 0 = キャンバス高さ（分割なし）
    bool debugCheckerboard = false;  // デバッグ用: 市松模様スキップ

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics* perfMetrics = nullptr;
#endif

    // 実際のタイルサイズを取得（0は最大値扱い）
    int getEffectiveTileWidth() const {
        return (tileWidth <= 0) ? totalWidth : tileWidth;
    }

    int getEffectiveTileHeight() const {
        return (tileHeight <= 0) ? totalHeight : tileHeight;
    }

    // タイル数を取得
    int getTileCountX() const {
        int tw = getEffectiveTileWidth();
        return (tw > 0) ? (totalWidth + tw - 1) / tw : 1;
    }

    int getTileCountY() const {
        int th = getEffectiveTileHeight();
        return (th > 0) ? (totalHeight + th - 1) / th : 1;
    }
};

// 部分矩形要求（段階1で伝播）
struct RenderRequest {
    int width = 0;
    int height = 0;
    float originX = 0;     // バッファ内での基準点X位置
    float originY = 0;     // バッファ内での基準点Y位置

    bool isEmpty() const { return width <= 0 || height <= 0; }

    // マージン分拡大（フィルタ用）
    // 基準相対座標系で両側に margin 分拡大する
    RenderRequest expand(int margin) const {
        return {
            width + margin * 2, height + margin * 2,
            originX + margin, originY + margin  // バッファ内の基準点位置も調整
        };
    }

    // RenderContextからタイル要求を生成
    static RenderRequest fromTile(const RenderContext& ctx, int tileX, int tileY) {
        int tw = ctx.getEffectiveTileWidth();
        int th = ctx.getEffectiveTileHeight();
        int tileLeft = tileX * tw;
        int tileTop = tileY * th;
        return {
            std::min(tw, ctx.totalWidth - tileLeft),
            std::min(th, ctx.totalHeight - tileTop),
            // originX/Y はバッファ相対座標（タイル内での基準点位置）
            ctx.originX - tileLeft, ctx.originY - tileTop
        };
    }
};

// ========================================================================
// パフォーマンス計測（FLEXIMG_DEBUG有効時のみ）
// ========================================================================

#ifdef FLEXIMG_DEBUG_PERF_METRICS

namespace PerfMetricIndex {
    constexpr int Filter = 0;
    constexpr int Affine = 1;
    constexpr int Composite = 2;
    constexpr int Convert = 3;
    constexpr int Output = 4;
    constexpr int Count = 5;
}

struct PerfMetrics {
    uint32_t times[PerfMetricIndex::Count] = {};  // マイクロ秒
    int counts[PerfMetricIndex::Count] = {};

    void add(int index, uint32_t us) {
        times[index] += us;
        counts[index]++;
    }

    void reset() {
        std::fill(std::begin(times), std::end(times), 0u);
        std::fill(std::begin(counts), std::end(counts), 0);
    }
};

#else

// リリースビルド用のダミー構造体（最小サイズ）
struct PerfMetrics {
    void reset() {}
};

#endif // FLEXIMG_DEBUG_PERF_METRICS

// ========================================================================
// ノードグラフ構造定義
// ========================================================================

// 合成ノードの入力定義
struct CompositeInput {
    std::string id;

    CompositeInput() : id("") {}
    explicit CompositeInput(const std::string& inputId)
        : id(inputId) {}
};

// ノードグラフのノード定義
struct GraphNode {
    std::string type;  // "image", "filter", "composite", "affine", "output"
    std::string id;

    // image/output共通: 画像ライブラリのID
    // - imageノード: 入力画像を参照
    // - outputノード: 出力先バッファを参照
    int imageId;
    float srcOriginX; // 画像の原点X（ピクセル座標）
    float srcOriginY; // 画像の原点Y（ピクセル座標）

    // filter用
    std::string filterType;
    std::vector<float> filterParams;  // 複数パラメータ対応
    bool independent;

    // composite用
    std::vector<CompositeInput> compositeInputs;  // 動的な入力配列

    // affine用（アフィン変換ノード）
    // JS側で行列に統一されるため、行列のみ保持
    AffineMatrix affineMatrix;

    GraphNode() : imageId(-1), srcOriginX(0.0f), srcOriginY(0.0f),
                  independent(false) {}  // filterParamsはstd::vectorなので自動初期化
};

// ノードグラフの接続定義
struct GraphConnection {
    std::string fromNodeId;
    std::string fromPort;
    std::string toNodeId;
    std::string toPort;
};

// ========================================================================
// ノードグラフ評価エンジン
// ========================================================================

class NodeGraphEvaluator {
public:
    NodeGraphEvaluator(int canvasWidth, int canvasHeight);
    ~NodeGraphEvaluator();  // Pipeline の完全な定義が必要なため .cpp で実装

    // 画像ライブラリに画像を登録（入力/出力共通）
    void registerImage(int id, const ViewPort& view);
    void registerImage(int id, void* data, int width, int height,
                       PixelFormatID format = PixelFormatIDs::RGBA8_Straight);

    // ノードグラフ構造を設定
    void setNodes(const std::vector<GraphNode>& nodes);
    void setConnections(const std::vector<GraphConnection>& connections);

    // ノードグラフを評価（出力はOutputノードが参照するimageLibraryに書き込まれる）
    void evaluateGraph();

    // キャンバスサイズ変更
    void setCanvasSize(int width, int height);

    // 出力先原点（dstOrigin）を設定
    void setDstOrigin(float x, float y);

    // タイル分割サイズを設定（0 = 分割なし）
    void setTileSize(int width, int height);

    // デバッグ用市松模様スキップを設定
    void setDebugCheckerboard(bool enabled);

    // パフォーマンス計測結果を取得
    const PerfMetrics& getPerfMetrics() const { return perfMetrics; }

private:
    int canvasWidth;
    int canvasHeight;
    float dstOriginX;  // 出力先の基準点X（ピクセル座標）
    float dstOriginY;  // 出力先の基準点Y（ピクセル座標）

    // タイル分割設定
    int tileWidth_ = 0;   // 0 = 分割なし
    int tileHeight_ = 0;
    bool debugCheckerboard_ = false;

    std::vector<GraphNode> nodes;
    std::vector<GraphConnection> connections;

    // パイプラインベース評価システム
    std::unique_ptr<Pipeline> pipeline_;
    bool pipelineDirty_ = true;    // パイプライン再構築が必要かどうか

    // パイプラインを構築（必要な場合のみ）
    void buildPipelineIfNeeded();

    // パイプラインベースの評価
    void evaluateWithPipeline(const RenderContext& context);

    // 画像ライブラリ（入力/出力共通、ViewPortの参照を保持）
    std::map<int, ViewPort> imageLibrary;

    // パフォーマンス計測
    PerfMetrics perfMetrics;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_NODE_GRAPH_H
