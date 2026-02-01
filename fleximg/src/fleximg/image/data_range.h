#ifndef FLEXIMG_DATA_RANGE_H
#define FLEXIMG_DATA_RANGE_H

#include <cstdint>

namespace FLEXIMG_NAMESPACE {

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

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_DATA_RANGE_H
