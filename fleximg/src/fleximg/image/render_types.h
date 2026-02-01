#ifndef FLEXIMG_RENDER_TYPES_H
#define FLEXIMG_RENDER_TYPES_H

#include <utility>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "../core/common.h"
#include "../core/perf_metrics.h"
#include "../core/memory/allocator.h"
#include "image_buffer.h"
#include "data_range.h"

// 前方宣言（循環参照回避）
namespace FLEXIMG_NAMESPACE {
class ImageBufferEntryPool;
class ImageBufferSet;
}

// ImageBufferSetの完全定義をインクルード（RenderResponseで値として使用）
#include "image_buffer_set.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// PrepareStatus - ノードの準備状態
// ========================================================================
//
// 準備フェーズの状態を表す。
// - 最終状態（Prepared, CycleError, NoUpstream, NoDownstream）は exec() の戻り値として使用
// - 中間状態（Idle, Preparing）は prepare フェーズ中の一時的な状態
// - 成功 = 0、エラー = 正の値、中間状態 = 負の値
//

enum class PrepareStatus : int {
    // 最終状態（exec() の戻り値として使用）
    Prepared = 0,          // 準備完了（成功）
    CycleError = 1,        // 循環参照を検出
    NoUpstream = 2,        // 上流ノードが未接続
    NoDownstream = 3,      // 下流ノードが未接続

    // 中間状態（prepare フェーズ中の一時的な状態）
    Idle = -2,             // 未処理（初期状態）
    Preparing = -1,        // 準備中（循環検出用）
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
    // origin は左上に移動（ワールド座標なので減算）
    RenderRequest expand(int margin) const {
        int_fixed marginFixed = to_fixed(margin);
        return {
            static_cast<int16_t>(width + margin * 2),
            static_cast<int16_t>(height + margin * 2),
            {origin.x - marginFixed, origin.y - marginFixed}
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

    // エントリプール（RendererNodeから伝播、ImageBufferSet用）
    ImageBufferEntryPool* entryPool = nullptr;

    // 希望フォーマット（下流から上流へ伝播、フォーマット交渉用）
    PixelFormatID preferredFormat = PixelFormatIDs::RGBA8_Straight;
};

// ========================================================================
// PrepareResponse - 準備応答（末端ノードからの応答）
// ========================================================================
//
// pushPrepare/pullPrepareの戻り値として使用。
// 末端ノード（SinkNode/SourceNode）が累積行列からAABBを計算して返す。
// 状態（status）と境界情報（AABB）を保持する。
//
// DataRange は data_range.h で定義

struct PrepareResponse {
    PrepareStatus status = PrepareStatus::Idle;

    // === AABBバウンディングボックス（処理すべき範囲） ===
    int16_t width = 0;
    int16_t height = 0;
    Point origin;

    // === フォーマット情報 ===
    PixelFormatID preferredFormat = PixelFormatIDs::RGBA8_Straight;

    // 便利メソッド
    bool ok() const { return status == PrepareStatus::Prepared; }

    // 要求矩形との交差判定
    // このAABBとrequestの矩形が重なるかどうかを判定
    bool intersects(const RenderRequest& request) const {
        // サイズ0は交差しない
        if (width <= 0 || height <= 0) return false;
        if (request.width <= 0 || request.height <= 0) return false;

        // 各矩形のワールド座標範囲を計算
        // AABB（this）の範囲（originはバッファ左上のワールド座標）
        float aabbLeft = fixed_to_float(origin.x);
        float aabbTop = fixed_to_float(origin.y);
        float aabbRight = aabbLeft + static_cast<float>(width);
        float aabbBottom = aabbTop + static_cast<float>(height);

        // リクエスト矩形の範囲（originはリクエスト左上のワールド座標）
        float reqLeft = fixed_to_float(request.origin.x);
        float reqTop = fixed_to_float(request.origin.y);
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

        // 各矩形のワールド座標範囲を計算
        // originはバッファ左上のワールド座標
        float aabbLeft = fixed_to_float(origin.x);
        float aabbTop = fixed_to_float(origin.y);
        float aabbRight = aabbLeft + static_cast<float>(width);
        float aabbBottom = aabbTop + static_cast<float>(height);

        // リクエスト範囲（originはリクエスト左上のワールド座標）
        float reqLeft = fixed_to_float(request.origin.x);
        float reqTop = fixed_to_float(request.origin.y);
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

// 入力矩形にアフィン変換を適用し、出力AABB（軸並行バウンディングボックス）を計算
//
// パラメータ:
//   inputWidth/Height: 入力矩形サイズ
//   inputOrigin: 入力矩形の基準点（pivot、バッファ座標、固定小数点）
//   matrix: 適用するアフィン変換（tx/ty が position を含む）
//
// 出力:
//   outWidth/Height: 変換後のAABBサイズ（ceilで切り上げ）
//   outOrigin: AABBの左上座標（ワールド座標、固定小数点）
//
// 最適化:
//   - 共通乗算の事前計算（a*left, a*right, c*left, c*right）で乗算4回削減
//   - tx/ty の加算を最後に1回だけ行う（8回→2回）
//   - std::min/max の initializer_list 版で簡潔に記述
inline void calcAffineAABB(
    int inputWidth, int inputHeight,
    Point inputOrigin,
    const AffineMatrix& matrix,
    int16_t& outWidth, int16_t& outHeight, Point& outOrigin)
{
    // 入力矩形の4角（pivot を原点とした相対座標）
    const float left = -fixed_to_float(inputOrigin.x);
    const float right = left + static_cast<float>(inputWidth);
    const float top = -fixed_to_float(inputOrigin.y);
    const float bottom = top + static_cast<float>(inputHeight);

    // X座標: 4角をアフィン変換してAABBを計算
    // x' = a*x + b*y + tx  (tx は最後に加算)
    const float al = matrix.a * left;
    const float ar = matrix.a * right;
    const float x0 = al + matrix.b * top;
    const float x1 = ar + matrix.b * top;
    const float x2 = al + matrix.b * bottom;
    const float x3 = ar + matrix.b * bottom;
    const float minX = std::min({x0, x1, x2, x3});
    const float maxX = std::max({x0, x1, x2, x3});

    outWidth = static_cast<int16_t>(std::ceil(maxX - minX));
    outOrigin.x = float_to_fixed(minX + matrix.tx);

    // Y座標: 同様に計算
    // y' = c*x + d*y + ty  (ty は最後に加算)
    const float cl = matrix.c * left;
    const float cr = matrix.c * right;
    const float y0 = cl + matrix.d * top;
    const float y1 = cr + matrix.d * top;
    const float y2 = cl + matrix.d * bottom;
    const float y3 = cr + matrix.d * bottom;
    const float minY = std::min({y0, y1, y2, y3});
    const float maxY = std::max({y0, y1, y2, y3});

    outHeight = static_cast<int16_t>(std::ceil(maxY - minY));
    outOrigin.y = float_to_fixed(minY + matrix.ty);
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
// RenderResponse - レンダリング応答
// ========================================================================
//
// 単純化されたRenderResponse:
// - 全てのバッファはImageBufferSet経由で管理
// - 単一バッファもImageBufferSetにラップして返す
// - ノード間でImageBufferSetをムーブで受け渡し
//

// 前方宣言
class ImageBufferSet;

struct RenderResponse {
    ImageBufferSet bufferSet;  // バッファセット（値所有）
    Point origin;              // バッファセット左上のワールド座標（固定小数点 Q16.16）

    // デフォルトコンストラクタ
    RenderResponse() = default;

    // ImageBufferSetムーブコンストラクタ
    RenderResponse(ImageBufferSet&& set, Point org)
        : bufferSet(std::move(set)), origin(org) {}

    // ImageBufferコンストラクタ（単一バッファをImageBufferSetにラップ）
    RenderResponse(ImageBuffer&& buf, Point org)
        : bufferSet(), origin(org) {
        if (buf.isValid()) {
            bufferSet.addBuffer(std::move(buf), 0);
        }
    }

    // ムーブのみ
    RenderResponse(const RenderResponse&) = delete;
    RenderResponse& operator=(const RenderResponse&) = delete;
    RenderResponse(RenderResponse&&) = default;
    RenderResponse& operator=(RenderResponse&&) = default;

    // ========================================
    // 有効性判定
    // ========================================

    /// @brief 有効なバッファを持っているか
    bool isValid() const { return !bufferSet.empty(); }

    /// @brief 空かどうか
    bool empty() const { return bufferSet.empty(); }

    /// @brief バッファ数を取得
    int bufferCount() const { return bufferSet.bufferCount(); }

    // ========================================
    // 単一バッファアクセス
    // ========================================

    /// @brief 単一バッファを取得（bufferCount()==1 前提）
    /// @note consolidate()後または単一エントリの場合に使用
    ImageBuffer& single() {
        FLEXIMG_ASSERT(bufferSet.bufferCount() == 1, "Expected single buffer");
        return bufferSet.buffer(0);
    }

    const ImageBuffer& single() const {
        FLEXIMG_ASSERT(bufferSet.bufferCount() == 1, "Expected single buffer");
        return bufferSet.buffer(0);
    }

    /// @brief 単一バッファのビューを取得
    ViewPort singleView() {
        return bufferSet.bufferCount() == 1 ? bufferSet.buffer(0).view() : ViewPort();
    }

    ViewPort singleView() const {
        return bufferSet.bufferCount() == 1 ? bufferSet.buffer(0).view() : ViewPort();
    }

    /// @brief 単一バッファのビューを取得（後方互換）
    ViewPort view() { return singleView(); }
    ViewPort view() const { return singleView(); }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_RENDER_TYPES_H
