#ifndef FLEXIMG_RENDERER_NODE_H
#define FLEXIMG_RENDERER_NODE_H

#include "../node.h"
#include "../render_types.h"
#include "../perf_metrics.h"
#include <algorithm>
#ifdef FLEXIMG_DEBUG_PERF_METRICS
#include <chrono>
#endif

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// RendererNode - パイプライン発火点ノード
// ========================================================================
//
// パイプライン実行の発火点となるノードです。
// - 入力: 1ポート（上流の処理ノード）
// - 出力: 1ポート（下流のSinkNode/DistributorNode）
// - 仮想スクリーンサイズと基準点を持つ
// - タイル分割処理を制御
//
// 使用例:
//   SourceNode src;
//   TransformNode transform;
//   RendererNode renderer;
//   SinkNode sink(output, 960, 540);
//
//   src >> transform >> renderer >> sink;
//
//   renderer.setVirtualScreen(1920, 1080, 960, 540);
//   renderer.setTileConfig({64, 64});
//   renderer.exec();
//

class RendererNode : public Node {
public:
    RendererNode() {
        initPorts(1, 1);  // 1入力・1出力
    }

    // ========================================
    // 設定API
    // ========================================

    // 仮想スクリーン設定
    void setVirtualScreen(int width, int height, float originX, float originY) {
        virtualWidth_ = width;
        virtualHeight_ = height;
        originX_ = originX;
        originY_ = originY;
    }

    void setVirtualScreen(int width, int height) {
        setVirtualScreen(width, height,
                         static_cast<float>(width) / 2,
                         static_cast<float>(height) / 2);
    }

    // タイル設定
    void setTileConfig(const TileConfig& config) {
        tileConfig_ = config;
    }

    void setTileConfig(int tileWidth, int tileHeight) {
        tileConfig_ = TileConfig(tileWidth, tileHeight);
    }

    // デバッグ用チェッカーボード
    void setDebugCheckerboard(bool enabled) {
        debugCheckerboard_ = enabled;
    }

    // アクセサ
    int virtualWidth() const { return virtualWidth_; }
    int virtualHeight() const { return virtualHeight_; }
    float originX() const { return originX_; }
    float originY() const { return originY_; }
    const TileConfig& tileConfig() const { return tileConfig_; }

    const char* name() const override { return "RendererNode"; }

    // ========================================
    // 実行API
    // ========================================

    // 簡易API（prepare → execute → finalize）
    void exec() {
        execPrepare();
        execProcess();
        execFinalize();
    }

    // 詳細API
    void execPrepare() {
        // メトリクスをリセット
        PerfMetrics::instance().reset();

        // スクリーン全体の情報をRenderRequestとして作成
        RenderRequest screenInfo = createScreenRequest();

        // 上流へ準備を伝播（プル型）
        Node* upstream = upstreamNode(0);
        if (upstream) {
            upstream->pullPrepare(screenInfo);
        }

        // 下流へ準備を伝播（プッシュ型）
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushPrepare(screenInfo);
        }
    }

    void execProcess() {
        int tileCountX = calcTileCountX();
        int tileCountY = calcTileCountY();

        for (int ty = 0; ty < tileCountY; ++ty) {
            for (int tx = 0; tx < tileCountX; ++tx) {
                // デバッグ用チェッカーボード: 市松模様でタイルをスキップ
                if (debugCheckerboard_ && ((tx + ty) % 2 == 1)) {
                    continue;
                }
                processTile(tx, ty);
            }
        }
    }

    void execFinalize() {
        // 上流へ終了を伝播（プル型）
        Node* upstream = upstreamNode(0);
        if (upstream) {
            upstream->pullFinalize();
        }

        // 下流へ終了を伝播（プッシュ型）
        Node* downstream = downstreamNode(0);
        if (downstream) {
            downstream->pushFinalize();
        }
    }

    // パフォーマンス計測結果を取得
    const PerfMetrics& getPerfMetrics() const {
        return PerfMetrics::instance();
    }

    void resetPerfMetrics() {
        PerfMetrics::instance().reset();
    }

protected:
    // タイル処理（派生クラスでカスタマイズ可能）
    virtual void processTile(int tileX, int tileY) {
#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto renderStart = std::chrono::high_resolution_clock::now();
#endif

        RenderRequest request = createTileRequest(tileX, tileY);

        // 上流からプル
        Node* upstream = upstreamNode(0);
        if (!upstream) return;

        RenderResult result = upstream->pullProcess(request);

        // 下流へプッシュ
        Node* downstream = downstreamNode(0);
        if (downstream && result.isValid()) {
            downstream->pushProcess(std::move(result), request);
        }

#ifdef FLEXIMG_DEBUG_PERF_METRICS
        auto& m = PerfMetrics::instance().nodes[NodeType::Output];
        m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - renderStart).count();
        m.count++;
#endif
    }

private:
    int virtualWidth_ = 0;
    int virtualHeight_ = 0;
    float originX_ = 0;
    float originY_ = 0;
    TileConfig tileConfig_;
    bool debugCheckerboard_ = false;

    // タイルサイズ取得
    int effectiveTileWidth() const {
        return tileConfig_.isEnabled() ? tileConfig_.tileWidth : virtualWidth_;
    }

    int effectiveTileHeight() const {
        return tileConfig_.isEnabled() ? tileConfig_.tileHeight : virtualHeight_;
    }

    // タイル数取得
    int calcTileCountX() const {
        int tw = effectiveTileWidth();
        return (tw > 0) ? (virtualWidth_ + tw - 1) / tw : 1;
    }

    int calcTileCountY() const {
        int th = effectiveTileHeight();
        return (th > 0) ? (virtualHeight_ + th - 1) / th : 1;
    }

    // スクリーン全体のRenderRequestを作成
    RenderRequest createScreenRequest() const {
        RenderRequest req;
        req.width = virtualWidth_;
        req.height = virtualHeight_;
        req.originX = originX_;
        req.originY = originY_;
        return req;
    }

    // タイル用のRenderRequestを作成
    RenderRequest createTileRequest(int tileX, int tileY) const {
        int tw = effectiveTileWidth();
        int th = effectiveTileHeight();
        int tileLeft = tileX * tw;
        int tileTop = tileY * th;

        // タイルサイズ（端の処理）
        int tileW = std::min(tw, virtualWidth_ - tileLeft);
        int tileH = std::min(th, virtualHeight_ - tileTop);

        RenderRequest req;
        req.width = tileW;
        req.height = tileH;
        req.originX = originX_ - tileLeft;
        req.originY = originY_ - tileTop;
        return req;
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_RENDERER_NODE_H
