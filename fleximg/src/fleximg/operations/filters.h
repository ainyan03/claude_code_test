#ifndef FLEXIMG_OPERATIONS_FILTERS_H
#define FLEXIMG_OPERATIONS_FILTERS_H

#include "../core/common.h"
#include "../image/viewport.h"

namespace FLEXIMG_NAMESPACE {
namespace filters {

// ========================================================================
// ラインフィルタ共通定義
// ========================================================================
//
// スキャンライン処理用の共通パラメータ構造体と関数型です。
// FilterNodeBase で使用され、派生クラスの共通化を実現します。
//

/// ラインフィルタ共通パラメータ
struct LineFilterParams {
    float value1 = 0.0f;  ///< brightness amount, alpha scale 等
    float value2 = 0.0f;  ///< 将来の拡張用
};

/// ラインフィルタ関数型（RGBA8_Straight形式、インプレース処理）
using LineFilterFunc = void(*)(uint8_t* pixels, int_fast16_t count, const LineFilterParams& params);

// ========================================================================
// ラインフィルタ関数（スキャンライン処理用）
// ========================================================================
//
// 1行分のピクセルデータ（RGBA8_Straight形式）を処理します。
// インプレース編集を前提とし、入力と出力は同一バッファです。
//

/// 明るさ調整（ラインフィルタ版）
/// params.value1: 明るさ調整量（-1.0〜1.0、0.5で+127相当）
void brightness_line(uint8_t* pixels, int_fast16_t count, const LineFilterParams& params);

/// グレースケール変換（ラインフィルタ版）
/// パラメータ未使用（将来の拡張用に引数は維持）
void grayscale_line(uint8_t* pixels, int_fast16_t count, const LineFilterParams& params);

/// アルファ調整（ラインフィルタ版）
/// params.value1: アルファスケール（0.0〜1.0）
void alpha_line(uint8_t* pixels, int_fast16_t count, const LineFilterParams& params);

} // namespace filters
} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

#include <algorithm>
#include <cstdint>

namespace FLEXIMG_NAMESPACE {
namespace filters {

// ========================================================================
// ラインフィルタ関数（スキャンライン処理用）
// ========================================================================

void brightness_line(uint8_t* pixels, int_fast16_t count, const LineFilterParams& params) {
    int adjustment = static_cast<int>(params.value1 * 255.0f);

    for (int_fast16_t x = 0; x < count; x++) {
        int_fast16_t pixelOffset = x * 4;
        // RGB各チャンネルに明るさ調整を適用
        for (int c = 0; c < 3; c++) {
            int value = static_cast<int>(pixels[pixelOffset + c]) + adjustment;
            pixels[pixelOffset + c] = static_cast<uint8_t>(std::max(0, std::min(255, value)));
        }
        // Alphaはそのまま維持
    }
}

void grayscale_line(uint8_t* pixels, int_fast16_t count, const LineFilterParams& params) {
    (void)params;  // 将来の拡張用に引数は維持

    for (int_fast16_t x = 0; x < count; x++) {
        int_fast16_t pixelOffset = x * 4;
        // グレースケール変換（平均法）
        uint8_t gray = static_cast<uint8_t>(
            (static_cast<uint16_t>(pixels[pixelOffset]) +
             static_cast<uint16_t>(pixels[pixelOffset + 1]) +
             static_cast<uint16_t>(pixels[pixelOffset + 2])) / 3
        );
        pixels[pixelOffset]     = gray;  // R
        pixels[pixelOffset + 1] = gray;  // G
        pixels[pixelOffset + 2] = gray;  // B
        // Alphaはそのまま維持
    }
}

void alpha_line(uint8_t* pixels, int_fast16_t count, const LineFilterParams& params) {
    uint32_t alphaScale = static_cast<uint32_t>(params.value1 * 256.0f);

    for (int_fast16_t x = 0; x < count; x++) {
        int_fast16_t pixelOffset = x * 4;
        // RGBはそのまま、Alphaのみスケール
        uint32_t a = pixels[pixelOffset + 3];
        pixels[pixelOffset + 3] = static_cast<uint8_t>((a * alphaScale) >> 8);
    }
}

} // namespace filters
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_OPERATIONS_FILTERS_H
