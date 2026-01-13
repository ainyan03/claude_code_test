#ifndef FLEXIMG_RENDERER_NODE_H
#define FLEXIMG_RENDERER_NODE_H

#include "../core/node.h"
#include "../core/types.h"
#include "../core/perf_metrics.h"
#include "../image/render_types.h"
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
//   AffineNode affine;
//   RendererNode renderer;
//   SinkNode sink(output, 960, 540);
//
//   src >> affine >> renderer >> sink;
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
    void setVirtualScreen(int width, int height, int_fixed8 originX, int_fixed8 originY) {
        virtualWidth_ = width;
        virtualHeight_ = height;
        originX_ = originX;
        originY_ = originY;
    }

    void setVirtualScreen(int width, int height) {
        setVirtualScreen(width, height,
                         to_fixed8(width / 2),
                         to_fixed8(height / 2));
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
    int_fixed8 originX() const { return originX_; }
    int_fixed8 originY() const { return originY_; }
    float originXf() const { return fixed8_to_float(originX_); }
    float originYf() const { return fixed8_to_float(originY_); }
    const TileConfig& tileConfig() const { return tileConfig_; }

    const char* name() const override { return "RendererNode"; }

    // ========================================
    // 実行API
    // ========================================

    // 簡易API（prepare → execute → finalize）
    // 戻り値: ExecResult（Success = 0、エラー = 非0）
    ExecResult exec() {
        ExecResult result = execPrepare();
        if (result != ExecResult::Success) {
            // エラー時も状態をリセット
            execFinalize();
            return result;
        }
        execProcess();
        execFinalize();
        return ExecResult::Success;
    }

    // 詳細API
    // 戻り値: ExecResult（Success = 0、エラー = 非0）
    ExecResult execPrepare() {
        // メトリクスをリセット
        PerfMetrics::instance().reset();

        // スクリーン全体の情報をRenderRequestとして作成
        RenderRequest screenInfo = createScreenRequest();

        // 上流へ準備を伝播（プル型、循環参照検出付き）
        Node* upstream = upstreamNode(0);
        if (upstream) {
            if (!upstream->pullPrepare(screenInfo)) {
                return ExecResult::CycleDetected;
            }
        }

        // 下流へ準備を伝播（プッシュ型、循環参照検出付き）
        Node* downstream = downstreamNode(0);
        if (downstream) {
            if (!downstream->pushPrepare(screenInfo)) {
                return ExecResult::CycleDetected;
            }
        }

        return ExecResult::Success;
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
        auto& m = PerfMetrics::instance().nodes[NodeType::Renderer];
        m.time_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - renderStart).count();
        m.count++;
#endif
    }

private:
    int virtualWidth_ = 0;
    int virtualHeight_ = 0;
    int_fixed8 originX_ = 0;
    int_fixed8 originY_ = 0;
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
        req.width = static_cast<int16_t>(virtualWidth_);
        req.height = static_cast<int16_t>(virtualHeight_);
        req.origin.x = originX_;
        req.origin.y = originY_;
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
        req.width = static_cast<int16_t>(tileW);
        req.height = static_cast<int16_t>(tileH);
        req.origin.x = originX_ - to_fixed8(tileLeft);
        req.origin.y = originY_ - to_fixed8(tileTop);
        return req;
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_RENDERER_NODE_H
