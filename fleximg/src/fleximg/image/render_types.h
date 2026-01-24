#ifndef FLEXIMG_RENDER_TYPES_H
#define FLEXIMG_RENDER_TYPES_H

#include <utility>
#include <cstdint>
#include <cmath>
#include "../core/common.h"
#include "../core/perf_metrics.h"
#include "../core/memory/allocator.h"
#include "image_buffer.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// PipelineStatus - パイプライン実行結果
// ========================================================================
//
// 成功 = 0、エラー = 非0（C言語の慣例に従う）
//

enum class PipelineStatus : int {
    Success = 0,           // 成功
    CycleDetected = 1,     // 循環参照を検出
    NoUpstream = 2,        // 上流ノードが未接続
    NoDownstream = 3,      // 下流ノードが未接続
};

// ========================================================================
// TileConfig - タイル分割設定
// ========================================================================

struct TileConfig {
    int16_t tileWidth = 0;   // 0 = 分割なし
    int16_t tileHeight = 0;

    TileConfig() = default;
    TileConfig(int w, int h)
        : tileWidth(static_cast<int16_t>(w))
        , tileHeight(static_cast<int16_t>(h)) {}

    bool isEnabled() const { return tileWidth > 0 && tileHeight > 0; }
};

// ========================================================================
// RenderRequest - 部分矩形要求
// ========================================================================

struct RenderRequest {
    int16_t width = 0;
    int16_t height = 0;
    Point origin;  // バッファ内での基準点位置（固定小数点 Q16.16）

    bool isEmpty() const { return width <= 0 || height <= 0; }

    // マージン分拡大（フィルタ用）
    // 左右上下に適用されるため width/height は margin*2 増加
    RenderRequest expand(int margin) const {
        int_fixed marginFixed = to_fixed(margin);
        return {
            static_cast<int16_t>(width + margin * 2),
            static_cast<int16_t>(height + margin * 2),
            {origin.x + marginFixed, origin.y + marginFixed}
        };
    }
};

// ========================================================================
// PrepareRequest - 準備リクエスト（アフィン伝播対応）
// ========================================================================
//
// pullPrepare時にアフィン変換パラメータを上流に伝播するための構造体。
// AffineNodeで行列を合成し、SourceNodeで一括実行する。
//

struct PrepareRequest {
    int16_t width = 0;
    int16_t height = 0;
    Point origin;  // 基準点位置（固定小数点 Q16.16）

    // プル型アフィン（上流→Source で実行）
    AffineMatrix affineMatrix;
    bool hasAffine = false;

    // プッシュ型アフィン（下流→Sink で実行）
    AffineMatrix pushAffineMatrix;
    bool hasPushAffine = false;

    // アロケータ（RendererNodeから伝播、各ノードがprepare時に保持）
    core::memory::IAllocator* allocator = nullptr;

    // 希望フォーマット（下流から上流へ伝播、フォーマット交渉用）
    PixelFormatID preferredFormat = PixelFormatIDs::RGBA8_Straight;
};

// ========================================================================
// PrepareResult - 準備結果（末端ノードからの応答）
// ========================================================================
//
// pushPrepare/pullPrepareの戻り値として使用。
// 末端ノード（SinkNode/SourceNode）が累積行列からAABBを計算して返す。
//

// ========================================================================
// DataRange - 有効データ範囲（X方向）
// ========================================================================
//
// スキャンライン処理（height=1）における有効X範囲を表す。
// CompositeNode等で上流のデータ範囲を事前に把握し、
// バッファサイズの最適化や範囲外スキップに使用。
//

struct DataRange {
    int16_t startX = 0;     // 有効開始X（request座標系）
    int16_t endX = 0;       // 有効終了X（request座標系）

    bool hasData() const { return startX < endX; }
    int16_t width() const { return (startX < endX) ? (endX - startX) : 0; }
};

struct PrepareResult {
    PipelineStatus status = PipelineStatus::Success;

    // === AABBバウンディングボックス（処理すべき範囲） ===
    int16_t width = 0;
    int16_t height = 0;
    Point origin;

    // === フォーマット情報 ===
    PixelFormatID preferredFormat = PixelFormatIDs::RGBA8_Straight;

    // 便利メソッド
    bool ok() const { return status == PipelineStatus::Success; }

    // 要求矩形との交差判定
    // このAABBとrequestの矩形が重なるかどうかを判定
    bool intersects(const RenderRequest& request) const {
        // サイズ0は交差しない
        if (width <= 0 || height <= 0) return false;
        if (request.width <= 0 || request.height <= 0) return false;

        // 各矩形の範囲を基準点からの相対座標で計算
        // AABB（this）の範囲
        float aabbLeft = -fixed_to_float(origin.x);
        float aabbTop = -fixed_to_float(origin.y);
        float aabbRight = aabbLeft + static_cast<float>(width);
        float aabbBottom = aabbTop + static_cast<float>(height);

        // リクエスト矩形の範囲
        float reqLeft = -fixed_to_float(request.origin.x);
        float reqTop = -fixed_to_float(request.origin.y);
        float reqRight = reqLeft + static_cast<float>(request.width);
        float reqBottom = reqTop + static_cast<float>(request.height);

        // 矩形が重ならない条件の否定
        return !(aabbRight <= reqLeft || reqRight <= aabbLeft ||
                 aabbBottom <= reqTop || reqBottom <= aabbTop);
    }

    // 要求矩形との交差範囲を取得（X方向）
    // request座標系でのX方向有効範囲を返す
    DataRange getDataRange(const RenderRequest& request) const {
        // サイズ0は空範囲
        if (width <= 0 || height <= 0) return DataRange{0, 0};
        if (request.width <= 0 || request.height <= 0) return DataRange{0, 0};

        // 各矩形の範囲を基準点からの相対座標で計算
        float aabbLeft = -fixed_to_float(origin.x);
        float aabbTop = -fixed_to_float(origin.y);
        float aabbRight = aabbLeft + static_cast<float>(width);
        float aabbBottom = aabbTop + static_cast<float>(height);

        float reqLeft = -fixed_to_float(request.origin.x);
        float reqTop = -fixed_to_float(request.origin.y);
        float reqRight = reqLeft + static_cast<float>(request.width);
        float reqBottom = reqTop + static_cast<float>(request.height);

        // Y方向の交差判定（交差しなければ空範囲）
        if (aabbBottom <= reqTop || reqBottom <= aabbTop) {
            return DataRange{0, 0};
        }

        // X方向の交差範囲を計算
        float intersectLeft = (aabbLeft > reqLeft) ? aabbLeft : reqLeft;
        float intersectRight = (aabbRight < reqRight) ? aabbRight : reqRight;

        if (intersectRight <= intersectLeft) {
            return DataRange{0, 0};
        }

        // request座標系に変換（reqLeftが0になる座標系）
        int16_t startX = static_cast<int16_t>(intersectLeft - reqLeft);
        int16_t endX = static_cast<int16_t>(std::ceil(intersectRight - reqLeft));

        // request.width内にクランプ
        if (startX < 0) startX = 0;
        if (endX > request.width) endX = request.width;

        return DataRange{startX, endX};
    }
};

// ========================================================================
// AABB計算ヘルパー関数
// ========================================================================
//
// アフィン変換適用時のバウンディングボックス計算。
// 末端ノード（SinkNode/SourceNode）がPrepareResult生成時に使用。
//

// 入力矩形にアフィン変換を適用し、出力AABBを計算
// inputWidth/Height: 入力矩形サイズ
// inputOrigin: 入力矩形の基準点（固定小数点）
// matrix: 適用するアフィン変換
// 戻り値: 変換後のAABB（width, height, origin）
inline void calcAffineAABB(
    int inputWidth, int inputHeight,
    Point inputOrigin,
    const AffineMatrix& matrix,
    int16_t& outWidth, int16_t& outHeight, Point& outOrigin)
{
    // 入力矩形の4角（基準点からの相対座標）
    float left = -fixed_to_float(inputOrigin.x);
    float top = -fixed_to_float(inputOrigin.y);
    float right = left + static_cast<float>(inputWidth);
    float bottom = top + static_cast<float>(inputHeight);

    // 4角をアフィン変換
    float x0 = matrix.a * left  + matrix.b * top    + matrix.tx;
    float y0 = matrix.c * left  + matrix.d * top    + matrix.ty;
    float x1 = matrix.a * right + matrix.b * top    + matrix.tx;
    float y1 = matrix.c * right + matrix.d * top    + matrix.ty;
    float x2 = matrix.a * left  + matrix.b * bottom + matrix.tx;
    float y2 = matrix.c * left  + matrix.d * bottom + matrix.ty;
    float x3 = matrix.a * right + matrix.b * bottom + matrix.tx;
    float y3 = matrix.c * right + matrix.d * bottom + matrix.ty;

    // AABBを求める
    float minX = x0, maxX = x0;
    float minY = y0, maxY = y0;
    if (x1 < minX) minX = x1; if (x1 > maxX) maxX = x1;
    if (x2 < minX) minX = x2; if (x2 > maxX) maxX = x2;
    if (x3 < minX) minX = x3; if (x3 > maxX) maxX = x3;
    if (y1 < minY) minY = y1; if (y1 > maxY) maxY = y1;
    if (y2 < minY) minY = y2; if (y2 > maxY) maxY = y2;
    if (y3 < minY) minY = y3; if (y3 > maxY) maxY = y3;

    // 結果を設定（ceilで切り上げてピクセル境界に合わせる）
    outWidth = static_cast<int16_t>(std::ceil(maxX - minX));
    outHeight = static_cast<int16_t>(std::ceil(maxY - minY));
    outOrigin.x = float_to_fixed(-minX);
    outOrigin.y = float_to_fixed(-minY);
}

// 出力矩形から逆変換で必要な入力範囲を計算
// outputWidth/Height: 出力矩形サイズ
// outputOrigin: 出力矩形の基準点（固定小数点）
// matrix: 順方向のアフィン変換（内部で逆行列を計算）
// 戻り値: 入力側で必要なAABB（width, height, origin）
inline void calcInverseAffineAABB(
    int outputWidth, int outputHeight,
    Point outputOrigin,
    const AffineMatrix& matrix,
    int16_t& outWidth, int16_t& outHeight, Point& outOrigin)
{
    // 逆行列を計算
    float det = matrix.a * matrix.d - matrix.b * matrix.c;
    if (std::abs(det) < 1e-10f) {
        // 特異行列の場合はそのまま返す
        outWidth = static_cast<int16_t>(outputWidth);
        outHeight = static_cast<int16_t>(outputHeight);
        outOrigin = outputOrigin;
        return;
    }

    float invDet = 1.0f / det;
    AffineMatrix inv(
        matrix.d * invDet,                  // a
        -matrix.b * invDet,                 // b
        -matrix.c * invDet,                 // c
        matrix.a * invDet,                  // d
        (matrix.b * matrix.ty - matrix.d * matrix.tx) * invDet,   // tx
        (matrix.c * matrix.tx - matrix.a * matrix.ty) * invDet    // ty
    );

    // 逆行列で変換
    calcAffineAABB(outputWidth, outputHeight, outputOrigin, inv,
                   outWidth, outHeight, outOrigin);
}

// ========================================================================
// RenderResult - 評価結果
// ========================================================================

struct RenderResult {
    ImageBuffer buffer;
    Point origin;  // バッファ内での基準点位置（固定小数点 Q16.16）

    RenderResult() = default;

    RenderResult(ImageBuffer&& buf, Point org)
        : buffer(std::move(buf)), origin(org) {}

    // ムーブのみ
    RenderResult(const RenderResult&) = delete;
    RenderResult& operator=(const RenderResult&) = delete;
    RenderResult(RenderResult&&) = default;
    RenderResult& operator=(RenderResult&&) = default;

    bool isValid() const { return buffer.isValid(); }
    ViewPort view() { return buffer.view(); }
    ViewPort view() const { return buffer.view(); }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_RENDER_TYPES_H
