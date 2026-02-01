#ifndef FLEXIMG_CANVAS_UTILS_H
#define FLEXIMG_CANVAS_UTILS_H

#include "../image/image_buffer.h"
#include "../image/render_types.h"

namespace FLEXIMG_NAMESPACE {
namespace canvas_utils {

// ========================================================================
// キャンバスユーティリティ
// ========================================================================
// CompositeNode や NinePatchSourceNode など、複数の画像を合成するノードで
// 共通して使用するキャンバス操作をまとめたユーティリティ関数群。
//

// ========================================================================
// RGBA8_Straight 形式のキャンバス操作（既存、over合成用）
// ========================================================================

// キャンバス作成（RGBA8_Straight形式）
// 合成処理に適したフォーマットでバッファを確保
// init: 初期化ポリシー（デフォルトはDefaultInitPolicy）
//   - 全面を画像で埋める場合: DefaultInitPolicy（初期化スキップ可）
//   - 部分的な描画の場合: InitPolicy::Zero（透明で初期化）
// alloc: メモリアロケータ（nullptrの場合はDefaultAllocator使用）
inline ImageBuffer createCanvas(int width, int height,
                                InitPolicy init = DefaultInitPolicy,
                                core::memory::IAllocator* alloc = nullptr) {
    return ImageBuffer(width, height, PixelFormatIDs::RGBA8_Straight, init, alloc);
}

// 最初の画像をキャンバスに配置
// 透明キャンバスへの最初の描画（ブレンド不要、変換コピーのみ）
// PixelFormatDescriptorの変換関数を使用
inline void placeFirst(ViewPort& canvas, int_fixed canvasOriginX, int_fixed canvasOriginY,
                       const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY) {
    if (!canvas.isValid() || !src.isValid()) return;

    // 新座標系: originはバッファ左上のワールド座標
    // srcをcanvasに配置する際のオフセット = src左端 - canvas左端
    int offsetX = from_fixed(srcOriginX - canvasOriginX);
    int offsetY = from_fixed(srcOriginY - canvasOriginY);

    // クリッピング範囲を計算
    // offsetX > 0: srcがcanvasより右にある → canvas[offsetX]に書き込み
    // offsetX < 0: srcがcanvasより左にある → src[-offsetX]から読み込み
    int srcStartX = std::max(0, -offsetX);
    int srcStartY = std::max(0, -offsetY);
    int dstStartX = std::max(0, offsetX);
    int dstStartY = std::max(0, offsetY);
    int copyWidth = std::min(src.width - srcStartX, canvas.width - dstStartX);
    int copyHeight = std::min(src.height - srcStartY, canvas.height - dstStartY);

    if (copyWidth <= 0 || copyHeight <= 0) return;

    // 同一フォーマット → memcpy
    if (src.formatID == canvas.formatID) {
        size_t bpp = static_cast<size_t>(getBytesPerPixel(src.formatID));
        for (int y = 0; y < copyHeight; y++) {
            const void* srcRow = src.pixelAt(srcStartX, srcStartY + y);
            void* dstRow = canvas.pixelAt(dstStartX, dstStartY + y);
            std::memcpy(dstRow, srcRow, static_cast<size_t>(copyWidth) * bpp);
        }
        return;
    }

    // キャンバスがRGBA8_Straight → toStraight関数を使用
    if (canvas.formatID == PixelFormatIDs::RGBA8_Straight && src.formatID->toStraight) {
        for (int y = 0; y < copyHeight; y++) {
            const void* srcRow = src.pixelAt(srcStartX, srcStartY + y);
            void* dstRow = canvas.pixelAt(dstStartX, dstStartY + y);
            src.formatID->toStraight(dstRow, srcRow, copyWidth, nullptr);
        }
        return;
    }

    // フォールバック: convertFormat経由（2段階変換の可能性あり）
    for (int y = 0; y < copyHeight; y++) {
        const void* srcRow = src.pixelAt(srcStartX, srcStartY + y);
        void* dstRow = canvas.pixelAt(dstStartX, dstStartY + y);
        convertFormat(srcRow, src.formatID, dstRow, canvas.formatID, copyWidth, nullptr);
    }
}

// フォーマット変換（必要なら）
// blend関数が対応していないフォーマットをRGBA8_Straightに変換
inline void ensureBlendableFormat(RenderResponse& input) {
    if (!input.isValid()) {
        return;
    }

    PixelFormatID inputFmt = input.view().formatID;
    if (inputFmt == PixelFormatIDs::RGBA8_Straight) {
        // 対応フォーマットならそのまま
        return;
    }

    // RGBA8_Straight に変換（bufferSet内で直接変換）
    input.bufferSet.convertFormat(PixelFormatIDs::RGBA8_Straight);
}

} // namespace canvas_utils
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_CANVAS_UTILS_H
