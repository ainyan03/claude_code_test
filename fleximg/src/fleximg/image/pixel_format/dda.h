#ifndef FLEXIMG_PIXEL_FORMAT_DDA_H
#define FLEXIMG_PIXEL_FORMAT_DDA_H

// pixel_format.h からインクルードされることを前提
// （PixelFormatDescriptor、DDAParam、int_fixed等は既に定義済み）

// ========================================================================
// DDA (Digital Differential Analyzer) 転写関数 - バイト単位実装
// ========================================================================
//
// ピクセルフォーマットに依存しないDDA処理の実装を集約。
// アフィン変換やバイリニア補間で使用される、高速なピクセル転写関数群。
//
// **このファイルの内容:**
// - バイト単位のDDA（1/2/3/4 バイト/ピクセル）
//   - copyRowDDA_Byte<BytesPerPixel>: 行単位ピクセル転写
//   - copyQuadDDA_Byte<BytesPerPixel>: 2x2グリッド抽出（バイリニア補間用）
//   - ラッパー関数: copyRowDDA_1Byte ～ _4Byte, copyQuadDDA_1Byte ～ _4Byte
//
// **bit-packed DDA関数の場所:**
// - ビット単位のDDA（1/2/4 ビット/ピクセル）は bit_packed_index.h 内に定義
//   - copyRowDDA_Bit<BitsPerPixel, Order>
//   - copyQuadDDA_Bit<BitsPerPixel, Order>
// - これは bit_packed_detail::readPixelDirect への依存を避けるための設計
//   （インクルード順序の複雑さを回避しつつ、命名規則の統一は達成）
//
// **命名規則の統一:**
// - バイト単位: _Byte サフィックス（明示的にバイト数を指定）
// - ビット単位: _Bit サフィックス（明示的にビット数とbit-orderを指定）
// - 対称性: copyRowDDA_Byte<3> vs copyRowDDA_Bit<4, MSBFirst>
//

#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {
namespace pixel_format {
namespace detail {

// ========================================================================
// バイト単位のDDA関数（1/2/3/4 バイト/ピクセル）
// ========================================================================

// BytesPerPixel → ネイティブ型マッピング（ロード・ストア分離用）
// 1, 2, 4 バイトはネイティブ型で直接ロード・ストア可能
// 3 バイトはネイティブ型が存在しないため byte 単位で処理
template<size_t BytesPerPixel> struct PixelType {};
template<> struct PixelType<1> { using type = uint8_t; };
template<> struct PixelType<2> { using type = uint16_t; };
template<> struct PixelType<4> { using type = uint32_t; };

// DDA行転写: Y座標一定パス（ソース行が同一の場合）
// srcRowBase = srcData + sy * srcStride（呼び出し前に計算済み）
// 4ピクセル単位展開でループオーバーヘッドを削減
template<size_t BytesPerPixel>
void copyRowDDA_ConstY(
    uint8_t* __restrict__ dstRow,
    const uint8_t* __restrict__ srcData,
    int count,
    const DDAParam* param
) {
    int_fixed srcX = param->srcX;
    const int_fixed incrX = param->incrX;
    const int32_t srcStride = param->srcStride;
    const uint8_t* srcRowBase = srcData + static_cast<size_t>((param->srcY >> INT_FIXED_SHIFT) * srcStride);

    // 端数を先に処理し、4ピクセルループを最後に連続実行する
    if constexpr (BytesPerPixel == 3) {
        if (count & 1) {
            // BytesPerPixel==3: byte単位でロード・ストア分離（3bytes × 4pixels）
            size_t s0 = static_cast<size_t>(srcX >> INT_FIXED_SHIFT) * 3;
            uint8_t p00 = srcRowBase[s0], p01 = srcRowBase[s0+1], p02 = srcRowBase[s0+2];
            srcX += incrX;
            dstRow[0]  = p00; dstRow[1]  = p01; dstRow[2]  = p02;
            dstRow += BytesPerPixel;
        }
        count >>= 1;
        while (count--) {
            // BytesPerPixel==3: byte単位でロード・ストア分離（3bytes × 4pixels）
            size_t s0 = static_cast<size_t>(srcX >> INT_FIXED_SHIFT) * 3;
            uint8_t p00 = srcRowBase[s0], p01 = srcRowBase[s0+1], p02 = srcRowBase[s0+2];
            srcX += incrX;
            dstRow[0]  = p00; dstRow[1]  = p01; dstRow[2]  = p02;

            size_t s1 = static_cast<size_t>(srcX >> INT_FIXED_SHIFT) * 3;
            uint8_t p10 = srcRowBase[s1], p11 = srcRowBase[s1+1], p12 = srcRowBase[s1+2];
            srcX += incrX;
            dstRow[3]  = p10; dstRow[4]  = p11; dstRow[5]  = p12;

            dstRow += BytesPerPixel * 2;
        }
    } else {
        using T = typename PixelType<BytesPerPixel>::type;
        auto src = reinterpret_cast<const T*>(srcRowBase);
        auto dst = reinterpret_cast<T*>(dstRow);
        int remainder = count & 3;
        for (int i = 0; i < remainder; i++) {
            // BytesPerPixel 1, 2, 4: ネイティブ型でロード・ストア分離
            auto p0 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            dst[0] = p0;
            dst += 1;
        }
        int count4 = count >> 2;
        for (int i = 0; i < count4; i++) {
            // BytesPerPixel 1, 2, 4: ネイティブ型でロード・ストア分離
            auto p0 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            auto p1 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            auto p2 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            auto p3 = src[srcX >> INT_FIXED_SHIFT]; srcX += incrX;
            dst[0] = p0;
            dst[1] = p1;
            dst[2] = p2;
            dst[3] = p3;
            dst += 4;
        }
    }
}

// DDA行転写: X座標一定パス（ソース列が同一の場合）
// srcColBase = srcData + sx * BytesPerPixel（呼び出し前に加算済み）
// 4ピクセル単位展開でループオーバーヘッドを削減
template<size_t BytesPerPixel>
void copyRowDDA_ConstX(
    uint8_t* __restrict__ dstRow,
    const uint8_t* __restrict__ srcData,
    int count,
    const DDAParam* param
) {
    int_fixed srcY = param->srcY;
    const int_fixed incrY = param->incrY;
    const int32_t srcStride = param->srcStride;
    const uint8_t* srcColBase = srcData + static_cast<size_t>((param->srcX >> INT_FIXED_SHIFT) * static_cast<int32_t>(BytesPerPixel));

    int32_t sy;
    // 端数を先に処理し、4ピクセルループを最後に連続実行する
    if constexpr (BytesPerPixel == 3) {
        while (count--) {
            // BytesPerPixel==3: byte単位でピクセルごとにロード・ストア
            sy = srcY >> INT_FIXED_SHIFT;
            const uint8_t* r = srcColBase + static_cast<size_t>(sy * srcStride);
            auto p0 = r[0], p1 = r[1], p2 = r[2];
            srcY += incrY;
            dstRow[0] = p0; dstRow[1] = p1; dstRow[2] = p2;
            dstRow += BytesPerPixel;
        }
    } else {
        using T = typename PixelType<BytesPerPixel>::type;
        auto dst = reinterpret_cast<T*>(dstRow);
        int remain = count & 3;
        while (remain--) {
            sy = srcY >> INT_FIXED_SHIFT;
            auto p = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy * srcStride));
            srcY += incrY;
            dst[0] = p;
            dst += 1;
        }

        count >>= 2;
        while (count--) {
            // BytesPerPixel 1, 2, 4: ネイティブ型でロード・ストア分離
            sy = srcY >> INT_FIXED_SHIFT;
            auto p0 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy * srcStride));
            srcY += incrY;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p1 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy * srcStride));
            srcY += incrY;
            dst[0] = p0;
            dst[1] = p1;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p2 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy * srcStride));
            srcY += incrY;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p3 = *reinterpret_cast<const T*>(srcColBase + static_cast<size_t>(sy * srcStride));
            srcY += incrY;
            dst[2] = p2;
            dst[3] = p3;
            dst += 4;
        }
    }

}

// DDA行転写の汎用実装（両方非ゼロ、回転を含む変換）
// 4ピクセル単位展開でループオーバーヘッドを削減
template<size_t BytesPerPixel>
void copyRowDDA_Impl(
    uint8_t* __restrict__ dstRow,
    const uint8_t* __restrict__ srcData,
    int count,
    const DDAParam* param
) {
    int_fixed srcY = param->srcY;
    int_fixed srcX = param->srcX;
    const int_fixed incrY = param->incrY;
    const int_fixed incrX = param->incrX;
    const int32_t srcStride = param->srcStride;

    int32_t sx, sy;
    // 端数を先に処理し、4ピクセルループを最後に連続実行する
    if constexpr (BytesPerPixel == 3) {
        while (count--) {
            // BytesPerPixel==3: byte単位でピクセルごとにロード・ストア
            sx = srcX >> INT_FIXED_SHIFT;
            sy = srcY >> INT_FIXED_SHIFT;
            const uint8_t* r0 = srcData + static_cast<size_t>(sy * srcStride + sx * 3);
            uint8_t p00 = r0[0], p01 = r0[1], p02 = r0[2];
            srcX += incrX; srcY += incrY;
            dstRow[0] = p00; dstRow[1] = p01; dstRow[2] = p02;
            dstRow += BytesPerPixel;
        }
    } else {
        using T = typename PixelType<BytesPerPixel>::type;
        auto d = reinterpret_cast<T*>(dstRow);
        if (count & 1) {
            sx = srcX >> INT_FIXED_SHIFT;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p = reinterpret_cast<const T*>(srcData + static_cast<size_t>(sy * srcStride))[sx];
            srcX += incrX; srcY += incrY;
            d[0] = p;
            d++;
        }

        count >>= 1;
        while (count--) {
            sx = srcX >> INT_FIXED_SHIFT;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p0 = reinterpret_cast<const T*>(srcData + static_cast<size_t>(sy * srcStride))[sx];
            srcX += incrX; srcY += incrY;
            sx = srcX >> INT_FIXED_SHIFT;
            sy = srcY >> INT_FIXED_SHIFT;
            auto p1 = reinterpret_cast<const T*>(srcData + static_cast<size_t>(sy * srcStride))[sx];
            srcX += incrX; srcY += incrY;
            // BytesPerPixel 1, 2, 4: ネイティブ型でロード・ストア分離
            d[0] = p0;
            d[1] = p1;
            d += 2;
        }
    }
}

// ============================================================================
// BytesPerPixel別 DDA転写関数（CopyRowDDA_Func シグネチャ準拠）
// ============================================================================
//
// PixelFormatDescriptor::copyRowDDA に設定する関数群。
//

template<size_t BytesPerPixel>
void copyRowDDA_Byte(
    uint8_t* dst,
    const uint8_t* srcData,
    int count,
    const DDAParam* param
) {
    const int_fixed srcY = param->srcY;
    const int_fixed incrY = param->incrY;
    // ソース座標の整数部が全ピクセルで同一か判定（座標は呼び出し側で非負が保証済み）
    if (0 == (((srcY & ((1 << INT_FIXED_SHIFT) - 1)) + incrY * count) >> INT_FIXED_SHIFT)) {
        // Y座標一定パス（高頻度: 回転なし拡大縮小・平行移動、微小Y変動も含む）
        copyRowDDA_ConstY<BytesPerPixel>(dst, srcData, count, param);
        return;
    }

    const int_fixed srcX = param->srcX;
    const int_fixed incrX = param->incrX;
    if (0 == (((srcX & ((1 << INT_FIXED_SHIFT) - 1)) + incrX * count) >> INT_FIXED_SHIFT)) {
        // X座標一定パス（微小X変動も含む）
        copyRowDDA_ConstX<BytesPerPixel>(dst, srcData, count, param);
        return;
    }

    // 汎用パス（回転を含む変換）
    copyRowDDA_Impl<BytesPerPixel>(dst, srcData, count, param);
}

// 明示的インスタンス化（各フォーマットから参照される）
template void copyRowDDA_Byte<1>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyRowDDA_Byte<2>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyRowDDA_Byte<3>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyRowDDA_Byte<4>(uint8_t*, const uint8_t*, int, const DDAParam*);

// BytesPerPixel別の関数ポインタ取得用ラッパー（非テンプレート）
inline void copyRowDDA_1Byte(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyRowDDA_Byte<1>(dst, srcData, count, param);
}
inline void copyRowDDA_2Byte(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyRowDDA_Byte<2>(dst, srcData, count, param);
}
inline void copyRowDDA_3Byte(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyRowDDA_Byte<3>(dst, srcData, count, param);
}
inline void copyRowDDA_4Byte(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyRowDDA_Byte<4>(dst, srcData, count, param);
}

// ============================================================================
// DDA 4ピクセル抽出関数（バイリニア補間用）
// ============================================================================
//
// バイリニア補間に必要な4ピクセル（2x2グリッド）を抽出する。
// 出力形式: [p00,p10,p01,p11][p00,p10,p01,p11]... × count
// 重み情報はparam->weightsに出力される。
//
// 最適化:
// - 案1: CheckBoundaryテンプレートパラメータで境界チェック有無を制御
// - 案2: コピー部分のみテンプレート化（copyQuadPixels）
//

// 4ピクセルのコピー（BytesPerPixel依存部分のみ）
template<size_t BytesPerPixel>
inline void copyQuadPixels(
    uint8_t* __restrict__ dst,
    const uint8_t* p00,
    const uint8_t* p10,
    const uint8_t* p01,
    const uint8_t* p11
) {
    if constexpr (BytesPerPixel == 3) {
        dst[0] = p00[0]; dst[1] = p00[1]; dst[2] = p00[2];
        dst[3] = p10[0]; dst[4] = p10[1]; dst[5] = p10[2];
        dst[6] = p01[0]; dst[7] = p01[1]; dst[8] = p01[2];
        dst[9] = p11[0]; dst[10] = p11[1]; dst[11] = p11[2];
    } else {
        using T = typename PixelType<BytesPerPixel>::type;
        auto d = reinterpret_cast<T*>(dst);
        auto d0 = *reinterpret_cast<const T*>(p00);
        auto d1 = *reinterpret_cast<const T*>(p10);
        auto d2 = *reinterpret_cast<const T*>(p01);
        auto d3 = *reinterpret_cast<const T*>(p11);
        d[0] = d0;
        d[1] = d1;
        d[2] = d2;
        d[3] = d3;
    }
}

// 4ピクセル抽出（DDAベース、バイリニア補間用）
// 境界領域と安全領域の2ブロック構成:
//   boundary [0, safeStart) → safe [safeStart, safeEnd) → boundary [safeEnd, count)
// fadeFlags は prepareCopyQuadDDA で事前生成済み、この関数では参照・更新しない
template<size_t BytesPerPixel>
void copyQuadDDA_Byte(
    uint8_t* __restrict__ dst,
    const uint8_t* __restrict__ srcData,
    int count,
    const DDAParam* param
) {
    constexpr size_t BPP = BytesPerPixel;
    constexpr size_t QUAD_SIZE = BPP * 4;

    int_fixed srcX = param->srcX;
    int_fixed srcY = param->srcY;
    const int_fixed incrX = param->incrX;
    const int_fixed incrY = param->incrY;
    const int32_t srcStride = param->srcStride;
    const int32_t srcLastX = param->srcWidth - 1;
    const int32_t srcLastY = param->srcHeight - 1;
    BilinearWeightXY* weightsXY = param->weightsXY;
    uint8_t* edgeFlags = param->edgeFlags;

    // 全ピクセル境界チェック版（事前範囲チェックなし）
    for (int i = 0; i < count; ++i) {
        int32_t sx = srcX >> INT_FIXED_SHIFT;
        int32_t sy = srcY >> INT_FIXED_SHIFT;
        weightsXY[i].fx = static_cast<uint8_t>(static_cast<uint32_t>(srcX) >> (INT_FIXED_SHIFT - 8));
        weightsXY[i].fy = static_cast<uint8_t>(static_cast<uint32_t>(srcY) >> (INT_FIXED_SHIFT - 8));
        srcX += incrX;
        srcY += incrY;
        bool x_sub = static_cast<uint32_t>(sx) < static_cast<uint32_t>(srcLastX);
        bool y_sub = static_cast<uint32_t>(sy) < static_cast<uint32_t>(srcLastY);

        if (x_sub && y_sub) {
            const uint8_t* p = srcData + static_cast<size_t>(sy) * static_cast<size_t>(srcStride) + static_cast<size_t>(sx) * BPP;
            edgeFlags[i] = 0;

            if constexpr (BPP == 3) {
                dst[0] = p[0]; dst[1] = p[1]; dst[2] = p[2];
                dst[3] = p[3]; dst[4] = p[4]; dst[5] = p[5];
                p += srcStride;
                dst[6] = p[0]; dst[7] = p[1]; dst[8] = p[2];
                dst[9] = p[3]; dst[10] = p[4]; dst[11] = p[5];
            } else {
                using T = typename PixelType<BPP>::type;
                auto d = reinterpret_cast<T*>(dst);
                auto val0 = reinterpret_cast<const T*>(p)[0];
                auto val1 = reinterpret_cast<const T*>(p)[1];
                d[0] = val0;
                d[1] = val1;
                p += srcStride;
                val0 = reinterpret_cast<const T*>(p)[0];
                val1 = reinterpret_cast<const T*>(p)[1];
                d[2] = val0;
                d[3] = val1;
            }
            dst += QUAD_SIZE;
        } else {
            // edgeFlags生成: 境界座標からフェードフラグを導出
            uint8_t flag_x = EdgeFade_Right;
            uint8_t flag_y = EdgeFade_Bottom;
            if (!x_sub) {
                if (sx < 0) {
                    sx = 0;
                    flag_x = EdgeFade_Left;
                }
            }
            if (!y_sub) {
                if (sy < 0) {
                    sy = 0;
                    flag_y = EdgeFade_Top;
                }
            }

            const uint8_t* p = srcData + static_cast<size_t>(sy) * static_cast<size_t>(srcStride) + static_cast<size_t>(sx) * BPP;

            if constexpr (BPP == 3) {
                auto val0 = p[0]; auto val1 = p[1]; auto val2 = p[2];
                dst[0] = val0; dst[1] = val1; dst[2] = val2;
                dst[3] = val0; dst[4] = val1; dst[5] = val2;
                dst[6] = val0; dst[7] = val1; dst[8] = val2;
                if (x_sub) {
                    val0 = p[3]; val1 = p[4]; val2 = p[5];
                    flag_x = 0;
                    dst[3] = val0; dst[4] = val1; dst[5] = val2;
                } else if (y_sub) {
                    p += srcStride;
                    val0 = p[0]; val1 = p[1]; val2 = p[2];
                    flag_y = 0;
                    dst[6] = val0; dst[7] = val1; dst[8] = val2;
                }
                dst[9] = val0; dst[10] = val1; dst[11] = val2;
            } else {
                using T = typename PixelType<BPP>::type;
                auto d = reinterpret_cast<T*>(dst);
                auto val = reinterpret_cast<const T*>(p)[0];
                d[0] = val; d[1] = val; d[2] = val;
                if (x_sub) {
                    val = reinterpret_cast<const T*>(p)[1];
                    flag_x = 0;
                    d[1] = val;
                } else if (y_sub) {
                    p += srcStride;
                    val = reinterpret_cast<const T*>(p)[0];
                    flag_y = 0;
                    d[2] = val;
                }
                d[3] = val;
            }
            edgeFlags[i] = flag_x + flag_y;
            dst += QUAD_SIZE;
        }
    }
}
// 明示的インスタンス化
template void copyQuadDDA_Byte<1>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyQuadDDA_Byte<2>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyQuadDDA_Byte<3>(uint8_t*, const uint8_t*, int, const DDAParam*);
template void copyQuadDDA_Byte<4>(uint8_t*, const uint8_t*, int, const DDAParam*);

// BytesPerPixel別の関数ポインタ取得用ラッパー（非テンプレート）
inline void copyQuadDDA_1Byte(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyQuadDDA_Byte<1>(dst, srcData, count, param);
}
inline void copyQuadDDA_2Byte(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyQuadDDA_Byte<2>(dst, srcData, count, param);
}
inline void copyQuadDDA_3Byte(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyQuadDDA_Byte<3>(dst, srcData, count, param);
}
inline void copyQuadDDA_4Byte(uint8_t* dst, const uint8_t* srcData, int count, const DDAParam* param) {
    copyQuadDDA_Byte<4>(dst, srcData, count, param);
}

// ========================================================================
// ビット単位のDDA関数（1/2/4 ビット/ピクセル、bit-packed形式）
// ========================================================================
//
// Bit-packed形式用のDDA転写関数。
// ピクセル単位でbit-packedデータから直接読み取り。
//
// 注意: bit_packed_detail::readPixelDirect を使用するため、
//       このセクションは bit_packed_index.h がインクルードされた後に
//       コンパイルされる必要があります。

// copyRowDDA_Bit: ピクセル単位で bit-packed から直接読み取り
template<int BitsPerPixel, BitOrder Order>
inline void copyRowDDA_Bit(
    uint8_t* dst,
    const uint8_t* srcData,
    int count,
    const DDAParam* param
) {
    // LovyanGFXスタイル: ピクセル単位で直接読み取り
    // 境界チェックは呼び出し側が保証（calcScanlineRange）
    int_fixed srcX = param->srcX;
    int_fixed srcY = param->srcY;
    const int_fixed incrX = param->incrX;
    const int_fixed incrY = param->incrY;
    const int32_t srcStride = param->srcStride;

    for (int i = 0; i < count; ++i) {
        int32_t sx = srcX >> INT_FIXED_SHIFT;
        int32_t sy = srcY >> INT_FIXED_SHIFT;
        srcX += incrX;
        srcY += incrY;

#ifdef FLEXIMG_DEBUG
        // デバッグビルドのみ: width/heightが設定されている場合は境界チェック
        if (param->srcWidth > 0 && param->srcHeight > 0) {
            FLEXIMG_REQUIRE(sx >= 0 && sx < param->srcWidth &&
                          sy >= 0 && sy < param->srcHeight,
                          "DDA out of bounds access");
        }
#endif

        dst[i] = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(
            srcData, sx, sy, srcStride);
    }
}

// copyQuadDDA_Bit: 2x2グリッドをピクセル単位で直接読み取り
template<int BitsPerPixel, BitOrder Order>
inline void copyQuadDDA_Bit(
    uint8_t* dst,
    const uint8_t* srcData,
    int count,
    const DDAParam* param
) {
    // LovyanGFXスタイル: 2x2グリッドを直接読み取り
    int_fixed srcX = param->srcX;
    int_fixed srcY = param->srcY;
    const int_fixed incrX = param->incrX;
    const int_fixed incrY = param->incrY;
    const int32_t srcWidth = param->srcWidth;
    const int32_t srcHeight = param->srcHeight;
    const int32_t srcStride = param->srcStride;
    BilinearWeightXY* weightsXY = param->weightsXY;
    uint8_t* edgeFlags = param->edgeFlags;

    for (int i = 0; i < count; ++i) {
        int32_t sx = srcX >> INT_FIXED_SHIFT;
        int32_t sy = srcY >> INT_FIXED_SHIFT;

        // バイリニア補間用の重み計算
        if (weightsXY) {
            weightsXY[i].fx = static_cast<uint8_t>(static_cast<uint32_t>(srcX) >> (INT_FIXED_SHIFT - 8));
            weightsXY[i].fy = static_cast<uint8_t>(static_cast<uint32_t>(srcY) >> (INT_FIXED_SHIFT - 8));
        }

        srcX += incrX;
        srcY += incrY;

        // 境界チェック（2x2グリッドが全て範囲内か）
        bool x_valid = (sx >= 0 && sx + 1 < srcWidth);
        bool y_valid = (sy >= 0 && sy + 1 < srcHeight);

        if (x_valid && y_valid) {
            // 全て範囲内: 2x2グリッドを読み取り
            dst[0] = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(srcData, sx,     sy,     srcStride);
            dst[1] = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(srcData, sx + 1, sy,     srcStride);
            dst[2] = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(srcData, sx,     sy + 1, srcStride);
            dst[3] = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(srcData, sx + 1, sy + 1, srcStride);
            if (edgeFlags) edgeFlags[i] = 0;
        } else {
            // 境界外を含む: copyQuadDDA_Byte と同じロジック
            uint8_t flag_x = EdgeFade_Right;
            uint8_t flag_y = EdgeFade_Bottom;

            // 座標をクランプ
            if (sx < 0) {
                sx = 0;
                flag_x = EdgeFade_Left;
            }
            if (sy < 0) {
                sy = 0;
                flag_y = EdgeFade_Top;
            }

            // 基準ピクセル（クランプした座標）を読む
            uint8_t val = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(
                srcData, sx, sy, srcStride);

            // 全て基準値で初期化
            dst[0] = val;
            dst[1] = val;
            dst[2] = val;

            // x方向が有効なら右隣を読む
            if (x_valid) {
                val = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(
                    srcData, sx + 1, sy, srcStride);
                dst[1] = val;
                flag_x = 0;
            } else if (y_valid) {
                // x方向無効でy方向有効なら下隣を読む
                val = bit_packed_detail::readPixelDirect<BitsPerPixel, Order>(
                    srcData, sx, sy + 1, srcStride);
                dst[2] = val;
                flag_y = 0;
            }

            dst[3] = val;  // 最後に読んだ値

            if (edgeFlags) edgeFlags[i] = flag_x + flag_y;
        }

        dst += 4;
    }
}

} // namespace detail
} // namespace pixel_format
} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_PIXEL_FORMAT_DDA_H
