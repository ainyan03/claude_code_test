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
//   static void rgb565le_blendUnderStraight(..., int pixelCount, ...) {
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
    constexpr uint_fast8_t RGBA8_Straight = 0;
    constexpr uint_fast8_t RGB565_LE = 1;
    constexpr uint_fast8_t RGB565_BE = 2;
    constexpr uint_fast8_t RGB332 = 3;
    constexpr uint_fast8_t RGB888 = 4;
    constexpr uint_fast8_t BGR888 = 5;
    constexpr uint_fast8_t Alpha8 = 6;
    constexpr uint_fast8_t Grayscale8 = 7;
    constexpr uint_fast8_t Index8 = 8;
    constexpr uint_fast8_t Count = 9;
}

// ========================================================================
// 操作タイプ
// ========================================================================

namespace OpType {
    constexpr uint_fast8_t ToStraight = 0;        // 各フォーマット → RGBA8_Straight
    constexpr uint_fast8_t FromStraight = 1;      // RGBA8_Straight → 各フォーマット
    constexpr uint_fast8_t BlendUnder = 2;        // 各フォーマット → Straight dst (under合成)
    constexpr uint_fast8_t Count = 3;
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

    void record(size_t pixels) {
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
        for (uint_fast8_t f = 0; f < FormatIdx::Count; ++f) {
            for (uint_fast8_t o = 0; o < OpType::Count; ++o) {
                data[f][o].reset();
            }
        }
    }

    void record(uint_fast8_t formatIdx, uint_fast8_t opType, size_t pixels) {
        if (formatIdx < FormatIdx::Count &&
            opType < OpType::Count) {
            data[formatIdx][opType].record(pixels);
        }
    }

    // 全フォーマットの合計（操作タイプ別）
    FormatOpEntry totalByOp(uint_fast8_t opType) const {
        FormatOpEntry total;
        if (opType < OpType::Count) {
            for (uint_fast8_t f = 0; f < FormatIdx::Count; ++f) {
                total.callCount += data[f][opType].callCount;
                total.pixelCount += data[f][opType].pixelCount;
            }
        }
        return total;
    }

    // 全操作の合計（フォーマット別）
    FormatOpEntry totalByFormat(uint_fast8_t formatIdx) const {
        FormatOpEntry total;
        if (formatIdx < FormatIdx::Count) {
            for (uint_fast8_t o = 0; o < OpType::Count; ++o) {
                total.callCount += data[formatIdx][o].callCount;
                total.pixelCount += data[formatIdx][o].pixelCount;
            }
        }
        return total;
    }

    // 全体合計
    FormatOpEntry total() const {
        FormatOpEntry t;
        for (uint_fast8_t f = 0; f < FormatIdx::Count; ++f) {
            for (uint_fast8_t o = 0; o < OpType::Count; ++o) {
                t.callCount += data[f][o].callCount;
                t.pixelCount += data[f][o].pixelCount;
            }
        }
        return t;
    }

    // スナップショット（現在の状態を保存）
    void saveSnapshot(FormatOpEntry snapshot[FormatIdx::Count][OpType::Count]) const {
        for (uint_fast8_t f = 0; f < FormatIdx::Count; ++f) {
            for (uint_fast8_t o = 0; o < OpType::Count; ++o) {
                snapshot[f][o] = data[f][o];
            }
        }
    }

    // スナップショットから復元
    void restoreSnapshot(const FormatOpEntry snapshot[FormatIdx::Count][OpType::Count]) {
        for (uint_fast8_t f = 0; f < FormatIdx::Count; ++f) {
            for (uint_fast8_t o = 0; o < OpType::Count; ++o) {
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
    void record(uint_fast8_t, uint_fast8_t, size_t) {}
    FormatOpEntry totalByOp(uint_fast8_t) const { return FormatOpEntry{}; }
    FormatOpEntry totalByFormat(uint_fast8_t) const { return FormatOpEntry{}; }
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
