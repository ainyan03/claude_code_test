#ifndef FLEXIMG_FORMAT_METRICS_H
#define FLEXIMG_FORMAT_METRICS_H

#include "common.h"
#include "perf_metrics.h"  // FLEXIMG_DEBUG_PERF_METRICS マクロ
#include <cstdint>

namespace FLEXIMG_NAMESPACE {
namespace core {

// ========================================================================
// フォーマット変換メトリクス
// ========================================================================
//
// ピクセルフォーマット変換・ブレンド関数の呼び出し回数とピクセル数を計測。
// FLEXIMG_DEBUG_PERF_METRICS が定義されている場合のみ有効。
//
// 使用例（各変換関数の先頭に1行追加）:
//   static void rgb565le_blendUnderPremul(..., int pixelCount, ...) {
//       FLEXIMG_FMT_METRICS(RGB565_LE, BlendUnder, pixelCount);
//       // 既存の処理...
//   }
//

// ========================================================================
// フォーマットインデックス
// ========================================================================
//
// 重要: PixelFormatIDs と同期を維持すること
//
// 新規フォーマット追加時の手順:
//   1. ここに新しいインデックスを追加（連番で）
//   2. Count を更新（最後のインデックス + 1）
//   3. pixel_format.h の PixelFormatIDs にも対応するIDがあることを確認
//   4. 該当フォーマットの変換関数に FLEXIMG_FMT_METRICS マクロを追加
//

namespace FormatIdx {
    constexpr int RGBA16_Premultiplied = 0;
    constexpr int RGBA8_Straight = 1;
    constexpr int RGB565_LE = 2;
    constexpr int RGB565_BE = 3;
    constexpr int RGB332 = 4;
    constexpr int RGB888 = 5;
    constexpr int BGR888 = 6;
    constexpr int Alpha8 = 7;
    constexpr int Count = 8;
}

// ========================================================================
// 操作タイプ
// ========================================================================

namespace OpType {
    constexpr int ToStraight = 0;        // 各フォーマット → RGBA8_Straight
    constexpr int FromStraight = 1;      // RGBA8_Straight → 各フォーマット
    constexpr int ToPremul = 2;          // 各フォーマット → RGBA16_Premultiplied
    constexpr int FromPremul = 3;        // RGBA16_Premultiplied → 各フォーマット
    constexpr int BlendUnder = 4;        // 各フォーマット → Premul dst (under合成)
    constexpr int BlendUnderStraight = 5;// 各フォーマット → Straight dst (under合成)
    constexpr int Count = 6;
}

// ========================================================================
// メトリクス構造体
// ========================================================================

#ifdef FLEXIMG_DEBUG_PERF_METRICS

struct FormatOpEntry {
    uint32_t callCount = 0;   // 呼び出し回数
    uint64_t pixelCount = 0;  // 処理ピクセル数

    void reset() {
        callCount = 0;
        pixelCount = 0;
    }

    void record(int pixels) {
        callCount++;
        pixelCount += static_cast<uint64_t>(pixels);
    }
};

struct FormatMetrics {
    FormatOpEntry data[FormatIdx::Count][OpType::Count];

    // シングルトンインスタンス
    static FormatMetrics& instance() {
        static FormatMetrics s_instance;
        return s_instance;
    }

    void reset() {
        for (int f = 0; f < FormatIdx::Count; ++f) {
            for (int o = 0; o < OpType::Count; ++o) {
                data[f][o].reset();
            }
        }
    }

    void record(int formatIdx, int opType, int pixels) {
        if (formatIdx >= 0 && formatIdx < FormatIdx::Count &&
            opType >= 0 && opType < OpType::Count) {
            data[formatIdx][opType].record(pixels);
        }
    }

    // 全フォーマットの合計（操作タイプ別）
    FormatOpEntry totalByOp(int opType) const {
        FormatOpEntry total;
        if (opType >= 0 && opType < OpType::Count) {
            for (int f = 0; f < FormatIdx::Count; ++f) {
                total.callCount += data[f][opType].callCount;
                total.pixelCount += data[f][opType].pixelCount;
            }
        }
        return total;
    }

    // 全操作の合計（フォーマット別）
    FormatOpEntry totalByFormat(int formatIdx) const {
        FormatOpEntry total;
        if (formatIdx >= 0 && formatIdx < FormatIdx::Count) {
            for (int o = 0; o < OpType::Count; ++o) {
                total.callCount += data[formatIdx][o].callCount;
                total.pixelCount += data[formatIdx][o].pixelCount;
            }
        }
        return total;
    }

    // 全体合計
    FormatOpEntry total() const {
        FormatOpEntry t;
        for (int f = 0; f < FormatIdx::Count; ++f) {
            for (int o = 0; o < OpType::Count; ++o) {
                t.callCount += data[f][o].callCount;
                t.pixelCount += data[f][o].pixelCount;
            }
        }
        return t;
    }

    // スナップショット（現在の状態を保存）
    void saveSnapshot(FormatOpEntry snapshot[FormatIdx::Count][OpType::Count]) const {
        for (int f = 0; f < FormatIdx::Count; ++f) {
            for (int o = 0; o < OpType::Count; ++o) {
                snapshot[f][o] = data[f][o];
            }
        }
    }

    // スナップショットから復元
    void restoreSnapshot(const FormatOpEntry snapshot[FormatIdx::Count][OpType::Count]) {
        for (int f = 0; f < FormatIdx::Count; ++f) {
            for (int o = 0; o < OpType::Count; ++o) {
                data[f][o] = snapshot[f][o];
            }
        }
    }
};

// 計測マクロ
#define FLEXIMG_FMT_METRICS(fmt, op, pixels) \
    ::FLEXIMG_NAMESPACE::core::FormatMetrics::instance().record( \
        ::FLEXIMG_NAMESPACE::core::FormatIdx::fmt, \
        ::FLEXIMG_NAMESPACE::core::OpType::op, \
        pixels)

#else

// リリースビルド用のダミー構造体
struct FormatOpEntry {
    void reset() {}
};

struct FormatMetrics {
    static FormatMetrics& instance() {
        static FormatMetrics s_instance;
        return s_instance;
    }
    void reset() {}
    void record(int, int, int) {}
    FormatOpEntry totalByOp(int) const { return FormatOpEntry{}; }
    FormatOpEntry totalByFormat(int) const { return FormatOpEntry{}; }
    FormatOpEntry total() const { return FormatOpEntry{}; }
    void saveSnapshot(FormatOpEntry[FormatIdx::Count][OpType::Count]) const {}
    void restoreSnapshot(const FormatOpEntry[FormatIdx::Count][OpType::Count]) {}
};

// リリースビルド用: メトリクス計測マクロは何もしない
#define FLEXIMG_FMT_METRICS(fmt, op, pixels) ((void)0)

#endif // FLEXIMG_DEBUG_PERF_METRICS

} // namespace core

// 親名前空間に公開
namespace FormatIdx = core::FormatIdx;
namespace OpType = core::OpType;
using core::FormatOpEntry;
using core::FormatMetrics;

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_FORMAT_METRICS_H
