#ifndef FLEXIMG_EVAL_RESULT_H
#define FLEXIMG_EVAL_RESULT_H

#include "common.h"
#include "image_buffer.h"
#include "viewport.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// EvalResult - パイプライン評価結果
// ========================================================================
//
// パイプライン処理における評価結果と座標情報を保持します。
// - ImageBuffer: 実際の画像データ（メモリ所有）
// - origin: 基準点からの相対座標
//
// 使用例:
//   EvalResult result = evaluateNode(...);
//   auto [offsetX, offsetY] = result.offsetTo(canvasOrigin);
//   canvas.blendOnto(result.view(), offsetX, offsetY);
//
struct EvalResult {
    ImageBuffer buffer;
    Point2f origin;         // 基準点からの相対座標（画像左上の位置）

    // デフォルトコンストラクタ
    EvalResult() : buffer(), origin(0, 0) {}

    // バッファと座標を指定
    EvalResult(ImageBuffer&& buf, Point2f org)
        : buffer(std::move(buf)), origin(org) {}

    EvalResult(ImageBuffer&& buf, float orgX, float orgY)
        : buffer(std::move(buf)), origin(orgX, orgY) {}

    // ムーブのみ許可（コピー禁止）
    EvalResult(const EvalResult&) = delete;
    EvalResult& operator=(const EvalResult&) = delete;
    EvalResult(EvalResult&&) = default;
    EvalResult& operator=(EvalResult&&) = default;

    // ========================================================================
    // ヘルパーメソッド
    // ========================================================================

    // ViewPortを取得
    ViewPort view() { return buffer.view(); }
    ViewPort view() const { return buffer.view(); }

    // 指定キャンバス座標へのオフセットを計算
    // canvasOrigin: キャンバス左上の基準相対座標
    // 戻り値: (offsetX, offsetY) - キャンバス上での配置位置
    std::pair<int, int> offsetTo(const Point2f& canvasOrigin) const {
        return {
            static_cast<int>(origin.x - canvasOrigin.x),
            static_cast<int>(origin.y - canvasOrigin.y)
        };
    }

    // 有効な結果か
    bool isValid() const { return buffer.isValid(); }

    // サイズ
    int width() const { return buffer.width; }
    int height() const { return buffer.height; }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_EVAL_RESULT_H
