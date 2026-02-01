/**
 * @file image_buffer_set.h
 * @brief 複数ImageBufferを重複なく保持するクラス（プール方式）
 */

#ifndef FLEXIMG_IMAGE_BUFFER_SET_H
#define FLEXIMG_IMAGE_BUFFER_SET_H

#include "image_buffer.h"
#include "image_buffer_entry_pool.h"
#include "data_range.h"
#include "../core/types.h"
#include "../core/memory/allocator.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// ImageBufferSet - 複数バッファ管理クラス（プール方式）
// ========================================================================
//
// 複数のImageBufferを重複なく保持し、効率的な合成・変換を行います。
// ImageBufferEntryPoolから取得したエントリへのポインタを保持する軽量設計。
//
// 重要: プール必須
// - ImageBufferEntryPoolが必須。プールなしではaddBuffer()が失敗します。
// - RendererNode経由でpool伝播されるため、通常のパイプライン使用では問題なし。
//
// 特徴:
// - ポインタ配列方式: 最大8エントリへのポインタを保持
// - プール連携: ImageBufferEntryPoolからacquire/release
// - 重複禁止: 登録時に重複があれば即座に合成処理を実行
// - 整数ピクセル単位: CompositeNodeと同様の座標管理
// - 軽量ムーブ: ポインタコピーのみ
//
// 合成ルール:
// - 同一フォーマット: 無変換でunder合成
// - 異なるフォーマット: RGBA8_Straightに変換して合成
//
// メモリ構成:
// - Entry* entryPtrs_[8]: 64バイト（8ポインタ × 8バイト）
// - pool_ポインタ: 8バイト
// - allocator_ポインタ: 8バイト
// - entryCount_: 4バイト
// - 合計: 約84バイト
//
// 使用例:
//   ImageBufferEntryPool pool;
//   ImageBufferSet set(&pool, allocator);
//   set.addBuffer(buffer1, 0);
//   set.addBuffer(buffer2, 50);  // 重複があれば自動合成
//   ImageBuffer result = set.consolidate();
//

class ImageBufferSet {
public:
    /// @brief 最大エントリ数（ImageBufferSet内の上限）
    static constexpr int MAX_ENTRIES = 8;

    /// @brief エントリ型（プールから取得）
    using Entry = ImageBufferEntryPool::Entry;

    // ========================================
    // 構築・破棄
    // ========================================

    /// @brief デフォルトコンストラクタ
    ImageBufferSet()
        : pool_(nullptr), allocator_(nullptr), entryCount_(0) {
        for (int i = 0; i < MAX_ENTRIES; ++i) {
            entryPtrs_[i] = nullptr;
        }
    }

    /// @brief コンストラクタ（プール指定）
    /// @param pool エントリプール
    /// @param allocator メモリアロケータ（合成用バッファ確保）
    explicit ImageBufferSet(ImageBufferEntryPool* pool,
                           core::memory::IAllocator* allocator = nullptr)
        : pool_(pool), allocator_(allocator), entryCount_(0) {
        for (int i = 0; i < MAX_ENTRIES; ++i) {
            entryPtrs_[i] = nullptr;
        }
    }

    /// @brief コンストラクタ（アロケータのみ、後方互換）
    /// @param allocator メモリアロケータ
    explicit ImageBufferSet(core::memory::IAllocator* allocator)
        : pool_(nullptr), allocator_(allocator), entryCount_(0) {
        for (int i = 0; i < MAX_ENTRIES; ++i) {
            entryPtrs_[i] = nullptr;
        }
    }

    /// @brief デストラクタ
    ~ImageBufferSet() {
        releaseAllEntries();
    }

    // コピー禁止
    ImageBufferSet(const ImageBufferSet&) = delete;
    ImageBufferSet& operator=(const ImageBufferSet&) = delete;

    // ムーブ許可（プールエントリのポインタをそのまま移転）
    ImageBufferSet(ImageBufferSet&& other) noexcept
        : pool_(other.pool_), allocator_(other.allocator_), entryCount_(other.entryCount_) {
        for (int i = 0; i < MAX_ENTRIES; ++i) {
            entryPtrs_[i] = other.entryPtrs_[i];
            other.entryPtrs_[i] = nullptr;
        }
        other.entryCount_ = 0;
    }

    ImageBufferSet& operator=(ImageBufferSet&& other) noexcept {
        if (this != &other) {
            releaseAllEntries();
            pool_ = other.pool_;
            allocator_ = other.allocator_;
            entryCount_ = other.entryCount_;
            for (int i = 0; i < MAX_ENTRIES; ++i) {
                entryPtrs_[i] = other.entryPtrs_[i];
                other.entryPtrs_[i] = nullptr;
            }
            other.entryCount_ = 0;
        }
        return *this;
    }

    // ========================================
    // バッファ登録
    // ========================================

    /// @brief バッファを登録
    /// @param buffer 追加するバッファ
    /// @param startX バッファの開始X座標（整数ピクセル単位）
    /// @return 成功時 true
    /// @note 重複があれば即座に合成処理を実行
    bool addBuffer(ImageBuffer&& buffer, int16_t startX);

    /// @brief バッファを登録（const参照版、コピーが発生）
    bool addBuffer(const ImageBuffer& buffer, int16_t startX);

    /// @brief バッファを登録（DataRange指定）
    bool addBuffer(ImageBuffer&& buffer, const DataRange& range);

    // ========================================
    // 変換・統合
    // ========================================

    /// @brief 全体を指定フォーマットに変換
    /// @param format 変換先フォーマット
    /// @param mergeAdjacent 隣接バッファを統合するか
    void convertFormat(PixelFormatID format, bool mergeAdjacent = false);

    /// @brief 単一バッファに統合して返す
    /// @param format 出力フォーマット（デフォルト: RGBA8_Straight）
    /// @return 統合されたバッファ
    ImageBuffer consolidate(PixelFormatID format = nullptr);

    /// @brief 隣接バッファを統合（ギャップが閾値以下の場合）
    /// @param gapThreshold ギャップ閾値（ピクセル単位）
    void mergeAdjacent(int16_t gapThreshold = 8);

    // ========================================
    // アクセス
    // ========================================

    /// @brief バッファ数を取得
    int bufferCount() const { return entryCount_; }

    /// @brief 指定インデックスのバッファを取得
    const ImageBuffer& buffer(int index) const {
        return entryPtrs_[index]->buffer;
    }

    /// @brief 指定インデックスのバッファを取得（非const）
    ImageBuffer& buffer(int index) {
        return entryPtrs_[index]->buffer;
    }

    /// @brief 指定インデックスの範囲を取得
    DataRange range(int index) const {
        return entryPtrs_[index]->range;
    }

    /// @brief 全体の範囲（最小startX〜最大endX）を取得
    DataRange totalRange() const {
        if (entryCount_ == 0) {
            return DataRange{0, 0};
        }
        return DataRange{
            entryPtrs_[0]->range.startX,
            entryPtrs_[entryCount_ - 1]->range.endX
        };
    }

    /// @brief 空かどうか
    bool empty() const { return entryCount_ == 0; }

    // ========================================
    // 状態管理
    // ========================================

    /// @brief クリア（エントリをプールに返却、再利用時）
    void clear() {
        releaseAllEntries();
    }

    /// @brief プールを設定
    void setPool(ImageBufferEntryPool* pool) {
        pool_ = pool;
    }

    /// @brief プールを取得
    ImageBufferEntryPool* pool() const { return pool_; }

    /// @brief アロケータを設定
    void setAllocator(core::memory::IAllocator* allocator) {
        allocator_ = allocator;
    }

    /// @brief アロケータを取得
    core::memory::IAllocator* allocator() const { return allocator_; }

private:
    Entry* entryPtrs_[MAX_ENTRIES];  ///< エントリポインタ配列（ソート済み）
    ImageBufferEntryPool* pool_;     ///< エントリプール
    core::memory::IAllocator* allocator_;  ///< メモリアロケータ（合成用）
    int entryCount_;  ///< 有効エントリ数

    // ========================================
    // 内部ヘルパー
    // ========================================

    /// @brief エントリを取得（プールまたはローカル）
    Entry* acquireEntry();

    /// @brief エントリを解放
    void releaseEntry(Entry* entry);

    /// @brief 全エントリを解放
    void releaseAllEntries();

    /// @brief ソート位置を見つけて挿入（重複なし前提）
    /// @param entry 挿入するエントリ
    /// @return 成功時 true
    bool insertSorted(Entry* entry);

    /// @brief 重複するエントリを検索
    /// @param range チェックする範囲
    /// @param outOverlapStart 重複開始インデックス（出力）
    /// @param outOverlapEnd 重複終了インデックス（出力、排他）
    /// @return 重複がある場合 true
    bool findOverlapping(const DataRange& range,
                         int& outOverlapStart, int& outOverlapEnd) const;

    /// @brief 重複部分を合成
    /// @param newEntry 新しいエントリ
    /// @param overlapStart 重複開始インデックス
    /// @param overlapEnd 重複終了インデックス
    /// @return 成功時 true
    bool mergeOverlapping(Entry* newEntry,
                          int overlapStart, int overlapEnd);
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

// 実装部で必要なヘッダはfleximg.cppで既にインクルード済み

namespace FLEXIMG_NAMESPACE {

// ----------------------------------------------------------------------------
// エントリ管理ヘルパー
// ----------------------------------------------------------------------------

ImageBufferSet::Entry* ImageBufferSet::acquireEntry() {
    // プールから取得（プール必須）
    if (pool_) {
        return pool_->acquire();
    }
    return nullptr;  // プールがない場合は失敗
}

void ImageBufferSet::releaseEntry(Entry* entry) {
    if (!entry || !pool_) return;
    pool_->release(entry);
}

void ImageBufferSet::releaseAllEntries() {
    for (int i = 0; i < entryCount_; ++i) {
        releaseEntry(entryPtrs_[i]);
        entryPtrs_[i] = nullptr;
    }
    entryCount_ = 0;
}

// ----------------------------------------------------------------------------
// addBuffer
// ----------------------------------------------------------------------------

bool ImageBufferSet::addBuffer(ImageBuffer&& buffer, int16_t startX) {
    // isValid()チェックはDataRange版で行うため省略
    DataRange range{startX, static_cast<int16_t>(startX + buffer.width())};
    return addBuffer(std::move(buffer), range);
}

bool ImageBufferSet::addBuffer(const ImageBuffer& buffer, int16_t startX) {
    ImageBuffer copy = buffer;  // コピー
    return addBuffer(std::move(copy), startX);
}

bool ImageBufferSet::addBuffer(ImageBuffer&& buffer, const DataRange& range) {
    if (!buffer.isValid()) {
        return false;
    }

    // エントリを取得
    Entry* entry = acquireEntry();
    if (!entry) {
        // プール枯渇時: consolidateして空きを作る
        if (entryCount_ > 0) {
            mergeAdjacent(0);  // 隣接を強制統合
            entry = acquireEntry();
        }
        if (!entry) {
            return false;  // それでもダメなら失敗
        }
    }

    entry->buffer = std::move(buffer);
    entry->range = range;

    // 空の場合は直接追加
    if (entryCount_ == 0) {
        entryPtrs_[0] = entry;
        entryCount_ = 1;
        return true;
    }

    // 重複チェック
    int overlapStart = 0, overlapEnd = 0;
    if (findOverlapping(range, overlapStart, overlapEnd)) {
        // 重複あり → 合成処理
        return mergeOverlapping(entry, overlapStart, overlapEnd);
    }

    // 重複なし → ソート位置に挿入
    return insertSorted(entry);
}

// ----------------------------------------------------------------------------
// insertSorted
// ----------------------------------------------------------------------------

bool ImageBufferSet::insertSorted(Entry* entry) {
    if (entryCount_ >= MAX_ENTRIES) {
        // 上限超過 → 隣接統合を試みる
        mergeAdjacent(0);  // 隣接を強制統合
        if (entryCount_ >= MAX_ENTRIES) {
            releaseEntry(entry);
            return false;  // それでもダメなら失敗
        }
    }

    const DataRange& range = entry->range;

    // 挿入位置を探す（startXの昇順）
    int insertPos = 0;
    while (insertPos < entryCount_ && entryPtrs_[insertPos]->range.startX < range.startX) {
        ++insertPos;
    }

    // 後ろをシフト
    for (int i = entryCount_; i > insertPos; --i) {
        entryPtrs_[i] = entryPtrs_[i - 1];
    }

    // 挿入
    entryPtrs_[insertPos] = entry;
    ++entryCount_;

    return true;
}

// ----------------------------------------------------------------------------
// findOverlapping
// ----------------------------------------------------------------------------

bool ImageBufferSet::findOverlapping(const DataRange& range,
                                     int& outOverlapStart, int& outOverlapEnd) const {
    outOverlapStart = -1;
    outOverlapEnd = -1;

    for (int i = 0; i < entryCount_; ++i) {
        const DataRange& existing = entryPtrs_[i]->range;

        // 重複判定: !(range.endX <= existing.startX || range.startX >= existing.endX)
        if (range.endX > existing.startX && range.startX < existing.endX) {
            if (outOverlapStart < 0) {
                outOverlapStart = i;
            }
            outOverlapEnd = i + 1;
        } else if (outOverlapStart >= 0) {
            // 既に重複範囲を見つけて、重複が終わった
            break;
        }
    }

    return outOverlapStart >= 0;
}

// ----------------------------------------------------------------------------
// mergeOverlapping
// ----------------------------------------------------------------------------

bool ImageBufferSet::mergeOverlapping(Entry* newEntry,
                                      int overlapStart, int overlapEnd) {
    if (!allocator_) {
        // アロケータがない場合は暫定的に追加
        return insertSorted(newEntry);
    }

    const DataRange& newRange = newEntry->range;

    // 合成対象の全体範囲を計算
    int16_t mergedStartX = newRange.startX;
    int16_t mergedEndX = newRange.endX;
    for (int i = overlapStart; i < overlapEnd; ++i) {
        mergedStartX = std::min(mergedStartX, entryPtrs_[i]->range.startX);
        mergedEndX = std::max(mergedEndX, entryPtrs_[i]->range.endX);
    }
    int mergedWidth = mergedEndX - mergedStartX;

    // 合成用バッファを確保（RGBA8_Straight）
    ImageBuffer mergedBuf(mergedWidth, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Zero, allocator_);
    if (!mergedBuf.isValid()) {
        return insertSorted(newEntry);
    }

    uint8_t* mergedRow = static_cast<uint8_t*>(mergedBuf.view().pixelAt(0, 0));
    constexpr size_t bytesPerPixel = 4;

    // 既存エントリを先に描画（under合成のため、先に描画したものが前面）
    for (int i = overlapStart; i < overlapEnd; ++i) {
        Entry* entry = entryPtrs_[i];
        int dstOffset = entry->range.startX - mergedStartX;
        int width = entry->range.endX - entry->range.startX;

        PixelFormatID srcFmt = entry->buffer.view().formatID;
        const uint8_t* srcRow = static_cast<const uint8_t*>(entry->buffer.view().pixelAt(0, 0));

        if (srcFmt == PixelFormatIDs::RGBA8_Straight) {
            // 同一フォーマット: 直接コピー（最初の描画なのでblend不要）
            std::memcpy(mergedRow + static_cast<size_t>(dstOffset) * bytesPerPixel,
                        srcRow, static_cast<size_t>(width) * bytesPerPixel);
        } else if (srcFmt->toStraight) {
            // フォーマット変換してコピー
            srcFmt->toStraight(mergedRow + static_cast<size_t>(dstOffset) * bytesPerPixel,
                               srcRow, width, nullptr);
        }
    }

    // 新バッファをunder合成
    {
        int dstOffset = newRange.startX - mergedStartX;
        int width = newRange.endX - newRange.startX;

        PixelFormatID srcFmt = newEntry->buffer.view().formatID;
        const uint8_t* srcRow = static_cast<const uint8_t*>(newEntry->buffer.view().pixelAt(0, 0));
        uint8_t* dstRow = mergedRow + static_cast<size_t>(dstOffset) * bytesPerPixel;

        if (srcFmt == PixelFormatIDs::RGBA8_Straight) {
            // 同一フォーマット: 直接under合成
            if (srcFmt->blendUnderStraight) {
                srcFmt->blendUnderStraight(dstRow, srcRow, width, nullptr);
            }
        } else if (srcFmt->blendUnderStraight) {
            // 直接under合成可能なフォーマット
            srcFmt->blendUnderStraight(dstRow, srcRow, width, nullptr);
        } else if (srcFmt->toStraight) {
            // 変換してからunder合成
            ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                                InitPolicy::Uninitialized, allocator_);
            if (tempBuf.isValid()) {
                srcFmt->toStraight(tempBuf.view().pixelAt(0, 0), srcRow, width, nullptr);
                PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
                    dstRow, tempBuf.view().pixelAt(0, 0), width, nullptr);
            }
        }
    }

    // 新エントリを解放（合成済み）
    releaseEntry(newEntry);

    // 古いエントリを削除（プールに返却）
    for (int i = overlapStart; i < overlapEnd; ++i) {
        releaseEntry(entryPtrs_[i]);
    }

    // ポインタ配列を詰める
    int removeCount = overlapEnd - overlapStart;
    for (int i = overlapStart; i < entryCount_ - removeCount; ++i) {
        entryPtrs_[i] = entryPtrs_[i + removeCount];
    }
    entryCount_ -= removeCount;

    // 合成結果用のエントリを取得
    Entry* resultEntry = acquireEntry();
    if (!resultEntry) {
        // 緊急時: 最初のエントリを再利用
        return false;
    }
    resultEntry->buffer = std::move(mergedBuf);
    resultEntry->range = DataRange{mergedStartX, mergedEndX};

    return insertSorted(resultEntry);
}

// ----------------------------------------------------------------------------
// convertFormat
// ----------------------------------------------------------------------------

void ImageBufferSet::convertFormat(PixelFormatID format, bool doMergeAdjacent) {
    if (entryCount_ == 0 || !allocator_ || !format) {
        return;
    }

    // 隣接統合オプション
    if (doMergeAdjacent) {
        mergeAdjacent(0);  // ギャップ0で隣接を統合
    }

    // 各エントリをフォーマット変換
    for (int i = 0; i < entryCount_; ++i) {
        Entry* entry = entryPtrs_[i];
        PixelFormatID srcFmt = entry->buffer.view().formatID;

        if (srcFmt == format) {
            continue;  // 変換不要
        }

        int width = entry->range.endX - entry->range.startX;

        // 変換先バッファを確保
        ImageBuffer converted(width, 1, format, InitPolicy::Uninitialized, allocator_);
        if (!converted.isValid()) {
            continue;
        }

        const void* srcRow = entry->buffer.view().pixelAt(0, 0);
        void* dstRow = converted.view().pixelAt(0, 0);

        // フォーマット変換
        if (format == PixelFormatIDs::RGBA8_Straight && srcFmt->toStraight) {
            srcFmt->toStraight(dstRow, srcRow, width, nullptr);
        } else if (srcFmt == PixelFormatIDs::RGBA8_Straight && format->fromStraight) {
            format->fromStraight(dstRow, srcRow, width, nullptr);
        } else {
            // 汎用変換（Straight経由）
            ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                                InitPolicy::Uninitialized, allocator_);
            if (tempBuf.isValid() && srcFmt->toStraight && format->fromStraight) {
                srcFmt->toStraight(tempBuf.view().pixelAt(0, 0), srcRow, width, nullptr);
                format->fromStraight(dstRow, tempBuf.view().pixelAt(0, 0), width, nullptr);
            }
        }

        entry->buffer = std::move(converted);
    }
}

// ----------------------------------------------------------------------------
// consolidate
// ----------------------------------------------------------------------------

ImageBuffer ImageBufferSet::consolidate(PixelFormatID format) {
    if (entryCount_ == 0) {
        return ImageBuffer();
    }

    // デフォルトフォーマット
    if (format == nullptr) {
        format = PixelFormatIDs::RGBA8_Straight;
    }

    // バッファが1つで、フォーマット変換不要なら、そのまま返す
    if (entryCount_ == 1) {
        if (entryPtrs_[0]->buffer.view().formatID == format) {
            ImageBuffer result = std::move(entryPtrs_[0]->buffer);
            releaseEntry(entryPtrs_[0]);
            entryPtrs_[0] = nullptr;
            entryCount_ = 0;
            return result;
        }
    }

    // 全体範囲を計算
    DataRange total = totalRange();
    int totalWidth = total.endX - total.startX;

    if (totalWidth <= 0 || !allocator_) {
        ImageBuffer result = std::move(entryPtrs_[0]->buffer);
        releaseAllEntries();
        return result;
    }

    // 出力バッファを確保（ゼロ初期化）
    ImageBuffer result(totalWidth, 1, format, InitPolicy::Zero, allocator_);
    if (!result.isValid()) {
        ImageBuffer fallback = std::move(entryPtrs_[0]->buffer);
        releaseAllEntries();
        return fallback;
    }

    uint8_t* dstRow = static_cast<uint8_t*>(result.view().pixelAt(0, 0));
    size_t dstBpp = static_cast<size_t>(getBytesPerPixel(format));

    // 各エントリを出力バッファにコピー
    for (int i = 0; i < entryCount_; ++i) {
        Entry* entry = entryPtrs_[i];
        int dstOffset = entry->range.startX - total.startX;
        int width = entry->range.endX - entry->range.startX;

        PixelFormatID srcFmt = entry->buffer.view().formatID;
        const void* srcRow = entry->buffer.view().pixelAt(0, 0);
        void* dstPtr = dstRow + static_cast<size_t>(dstOffset) * dstBpp;

        if (srcFmt == format) {
            // 同一フォーマット: 直接コピー
            std::memcpy(dstPtr, srcRow, static_cast<size_t>(width) * dstBpp);
        } else if (format == PixelFormatIDs::RGBA8_Straight && srcFmt->toStraight) {
            // RGBA8_Straightへ変換
            srcFmt->toStraight(dstPtr, srcRow, width, nullptr);
        } else if (srcFmt == PixelFormatIDs::RGBA8_Straight && format->fromStraight) {
            // RGBA8_Straightから変換
            format->fromStraight(dstPtr, srcRow, width, nullptr);
        } else {
            // 汎用変換（Straight経由）
            ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                                InitPolicy::Uninitialized, allocator_);
            if (tempBuf.isValid() && srcFmt->toStraight && format->fromStraight) {
                srcFmt->toStraight(tempBuf.view().pixelAt(0, 0), srcRow, width, nullptr);
                format->fromStraight(dstPtr, tempBuf.view().pixelAt(0, 0), width, nullptr);
            }
        }
    }

    releaseAllEntries();
    return result;
}

// ----------------------------------------------------------------------------
// mergeAdjacent
// ----------------------------------------------------------------------------

void ImageBufferSet::mergeAdjacent(int16_t gapThreshold) {
    if (entryCount_ < 2 || !allocator_) {
        return;
    }

    // 隣接エントリを統合（後ろから前へ処理して、インデックスのずれを回避）
    for (int i = entryCount_ - 1; i > 0; --i) {
        Entry* curr = entryPtrs_[i];
        Entry* prev = entryPtrs_[i - 1];

        int16_t gap = curr->range.startX - prev->range.endX;

        if (gap <= gapThreshold) {
            // 統合: prevとcurrをマージ
            int16_t mergedStartX = prev->range.startX;
            int16_t mergedEndX = curr->range.endX;
            int mergedWidth = mergedEndX - mergedStartX;

            // 統合バッファを確保
            ImageBuffer merged(mergedWidth, 1, PixelFormatIDs::RGBA8_Straight,
                               InitPolicy::Zero, allocator_);
            if (!merged.isValid()) {
                continue;
            }

            uint8_t* mergedRow = static_cast<uint8_t*>(merged.view().pixelAt(0, 0));
            constexpr size_t bytesPerPixel = 4;

            // prev をコピー
            {
                int width = prev->range.endX - prev->range.startX;
                PixelFormatID srcFmt = prev->buffer.view().formatID;
                const void* srcRow = prev->buffer.view().pixelAt(0, 0);

                if (srcFmt == PixelFormatIDs::RGBA8_Straight) {
                    std::memcpy(mergedRow, srcRow, static_cast<size_t>(width) * bytesPerPixel);
                } else if (srcFmt->toStraight) {
                    srcFmt->toStraight(mergedRow, srcRow, width, nullptr);
                }
            }

            // curr をコピー
            {
                int dstOffset = curr->range.startX - mergedStartX;
                int width = curr->range.endX - curr->range.startX;
                PixelFormatID srcFmt = curr->buffer.view().formatID;
                const void* srcRow = curr->buffer.view().pixelAt(0, 0);
                void* dstPtr = mergedRow + static_cast<size_t>(dstOffset) * bytesPerPixel;

                if (srcFmt == PixelFormatIDs::RGBA8_Straight) {
                    std::memcpy(dstPtr, srcRow, static_cast<size_t>(width) * bytesPerPixel);
                } else if (srcFmt->toStraight) {
                    srcFmt->toStraight(dstPtr, srcRow, width, nullptr);
                }
            }

            // prevを統合結果で置き換え
            prev->buffer = std::move(merged);
            prev->range = DataRange{mergedStartX, mergedEndX};

            // currを解放
            releaseEntry(curr);

            // ポインタ配列を詰める
            for (int j = i; j < entryCount_ - 1; ++j) {
                entryPtrs_[j] = entryPtrs_[j + 1];
            }
            entryPtrs_[entryCount_ - 1] = nullptr;
            --entryCount_;
        }
    }
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_IMAGE_BUFFER_SET_H
