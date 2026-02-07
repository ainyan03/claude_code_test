#ifndef FLEXIMG_PIXEL_FORMAT_DDA_H
#define FLEXIMG_PIXEL_FORMAT_DDA_H

// ========================================================================
// DDA (Digital Differential Analyzer) 転写関数
// ========================================================================
//
// ピクセルフォーマットに依存しないDDA処理の実装を集約。
// アフィン変換やバイリニア補間で使用される、高速なピクセル転写関数群。
//
// - バイト単位のDDA（1/2/3/4 バイト/ピクセル）
// - ビット単位のDDA（1/2/4 ビット/ピクセル、bit-packed形式）
//

// pixel_format.hから必要な型定義をインクルード
// （前方宣言部分のみ、実装部分は除外）

namespace FLEXIMG_NAMESPACE {
namespace pixel_format {
namespace detail {

// ========================================================================
// バイト単位のDDA関数（1/2/3/4 バイト/ピクセル）
// ========================================================================
//
// Non-bit-packed形式用のDDA転写関数。
// copyRowDDA: 1次元スキャンライン転写
// copyQuadDDA: 2x2ピクセルグリッド抽出（バイリニア補間用）
//

// TODO: Phase 2 で pixel_format.h から移動
// - copyRowDDA_Byte<N> テンプレート実装
// - copyQuadDDA_Byte<N> テンプレート実装
// - copyRowDDA_1Byte～4Byte ラッパー関数
// - copyQuadDDA_1Byte～4Byte ラッパー関数

// ========================================================================
// ビット単位のDDA関数（1/2/4 ビット/ピクセル、bit-packed形式）
// ========================================================================
//
// Bit-packed形式用のDDA転写関数。
// ピクセル単位でbit-packedデータから直接読み取り。
//

// TODO: Phase 3 で bit_packed_index.h から移動
// - copyRowDDA_Bit<N, Order> テンプレート実装
// - copyQuadDDA_Bit<N, Order> テンプレート実装

} // namespace detail
} // namespace pixel_format
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_PIXEL_FORMAT_DDA_H
