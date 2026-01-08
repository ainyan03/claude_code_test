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
struct Image;
struct Pipeline;
class EvaluationNode;

// ========================================================================
// タイル分割評価システム用構造体
// ========================================================================

// タイル分割戦略
enum class TileStrategy {
    None,       // 分割なし（従来互換、全体を一度に処理）
    Scanline,   // 1行ずつ処理（極小メモリ環境向け）
    Tile64,     // 64x64タイル（組込み環境標準）
    Custom,     // カスタムサイズ
    Debug_Checkerboard  // デバッグ用: 市松模様（交互にスキップ）
};

// 前方宣言
struct PerfMetrics;

// 出力全体情報（段階0で伝播）
struct RenderContext {
    int totalWidth = 0;
    int totalHeight = 0;
    float originX = 0;     // dstOrigin X
    float originY = 0;     // dstOrigin Y

    TileStrategy strategy = TileStrategy::None;
    int tileWidth = 64;     // Custom用
    int tileHeight = 64;

#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics* perfMetrics = nullptr;
#endif

    // タイル数を取得
    int getTileCountX() const {
        if (strategy == TileStrategy::None) return 1;
        int tw = (strategy == TileStrategy::Scanline) ? totalWidth :
                 (strategy == TileStrategy::Tile64) ? 64 : tileWidth;
        return (totalWidth + tw - 1) / tw;
    }

    int getTileCountY() const {
        if (strategy == TileStrategy::None) return 1;
        int th = (strategy == TileStrategy::Scanline) ? 1 :
                 (strategy == TileStrategy::Tile64) ? 64 : tileHeight;
        return (totalHeight + th - 1) / th;
    }

    // 実際のタイルサイズを取得
    int getEffectiveTileWidth() const {
        if (strategy == TileStrategy::None) return totalWidth;
        if (strategy == TileStrategy::Scanline) return totalWidth;
        if (strategy == TileStrategy::Tile64) return 64;
        if (strategy == TileStrategy::Debug_Checkerboard) return 64;
        return tileWidth;
    }

    int getEffectiveTileHeight() const {
        if (strategy == TileStrategy::None) return totalHeight;
        if (strategy == TileStrategy::Scanline) return 1;
        if (strategy == TileStrategy::Tile64) return 64;
        if (strategy == TileStrategy::Debug_Checkerboard) return 64;
        return tileHeight;
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

    // image用
    int imageId;       // 画像ライブラリのID
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

    // 画像ライブラリに画像を登録（8bit RGBA）
    void registerImage(int imageId, const Image& img);

    // ノードグラフ構造を設定
    void setNodes(const std::vector<GraphNode>& nodes);
    void setConnections(const std::vector<GraphConnection>& connections);

    // ノードグラフを評価して最終画像を取得（1回のWASM呼び出しで完結）
    Image evaluateGraph();

    // キャンバスサイズ変更
    void setCanvasSize(int width, int height);

    // 出力先原点（dstOrigin）を設定
    void setDstOrigin(float x, float y);

    // タイル分割戦略を設定
    void setTileStrategy(TileStrategy strategy, int tileWidth = 64, int tileHeight = 64);

    // パフォーマンス計測結果を取得
    const PerfMetrics& getPerfMetrics() const { return perfMetrics; }

private:
    int canvasWidth;
    int canvasHeight;
    float dstOriginX;  // 出力先の基準点X（ピクセル座標）
    float dstOriginY;  // 出力先の基準点Y（ピクセル座標）

    // タイル分割設定
    TileStrategy tileStrategy = TileStrategy::None;
    int customTileWidth = 64;
    int customTileHeight = 64;

    std::vector<GraphNode> nodes;
    std::vector<GraphConnection> connections;

    // パイプラインベース評価システム
    std::unique_ptr<Pipeline> pipeline_;
    bool pipelineDirty_ = true;    // パイプライン再構築が必要かどうか

    // パイプラインを構築（必要な場合のみ）
    void buildPipelineIfNeeded();

    // パイプラインベースの評価
    Image evaluateWithPipeline(const RenderContext& context);

    // 画像ライブラリ（RGBA8_Straight形式のImageBufferで保存）
    std::map<int, ImageBuffer> imageLibrary;

    // パフォーマンス計測
    PerfMetrics perfMetrics;
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_NODE_GRAPH_H
