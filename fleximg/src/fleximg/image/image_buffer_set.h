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
    static constexpr int MAX_ENTRIES = 32;

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

    /// @brief 他のImageBufferSetからエントリをバッチ転送
    /// @param source 転送元（転送後は空になる）
    /// @param offsetX 各エントリに加算するXオフセット
    /// @return 成功時 true
    /// @note プール操作なしでエントリポインタを直接移動
    bool transferFrom(ImageBufferSet& source, int16_t offsetX);

    /// @brief 全エントリにXオフセットを適用
    /// @param offsetX 各エントリに加算するXオフセット
    void applyOffset(int16_t offsetX);

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

    /// @brief その場で統合（最初のエントリを再利用、フォーマット変換なし）
    /// @note フォーマット変換はNode::convertFormat()経由で行う（メトリクス記録のため）
    /// @note 統合後は entryCount_ == 1 または 0（空の場合）となる
    void consolidateInPlace();

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

    /// @brief 指定インデックスのバッファを入れ替え
    /// @param index エントリインデックス
    /// @param buffer 新しいバッファ
    /// @note エントリ自体は再利用（acquire/releaseなし）
    void replaceBuffer(int index, ImageBuffer&& buffer);

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

void ImageBufferSet::applyOffset(int16_t offsetX) {
    if (offsetX == 0) return;
    for (int i = 0; i < entryCount_; ++i) {
        Entry* entry = entryPtrs_[i];
        if (!entry) continue;  // 防御的チェック
        entry->range.startX = static_cast<int16_t>(entry->range.startX + offsetX);
        entry->range.endX = static_cast<int16_t>(entry->range.endX + offsetX);
    }
}

bool ImageBufferSet::transferFrom(ImageBufferSet& source, int16_t offsetX) {
#ifdef FLEXIMG_DEBUG
    // デバッグ: thisとsourceの整合性チェック
    if (entryCount_ < 0 || entryCount_ > MAX_ENTRIES) {
        printf("ERROR: transferFrom this=%p entryCount_=%d (MAX=%d)\n",
               static_cast<void*>(this), entryCount_, MAX_ENTRIES);
        fflush(stdout);
#ifdef ARDUINO
        vTaskDelay(1);
#endif
    }
    FLEXIMG_ASSERT(entryCount_ >= 0 && entryCount_ <= MAX_ENTRIES, "transferFrom: invalid entryCount_");
    FLEXIMG_ASSERT(source.entryCount_ >= 0 && source.entryCount_ <= MAX_ENTRIES, "transferFrom: invalid source.entryCount_");
    for (int i = 0; i < entryCount_; ++i) {
        if (!entryPtrs_[i] || !entryPtrs_[i]->inUse || !entryPtrs_[i]->buffer.isValid()) {
            printf("ERROR: this[%d]=%p inUse=%d valid=%d\n", i,
                   static_cast<void*>(entryPtrs_[i]),
                   entryPtrs_[i] ? entryPtrs_[i]->inUse : -1,
                   entryPtrs_[i] ? entryPtrs_[i]->buffer.isValid() : -1);
            fflush(stdout);
#ifdef ARDUINO
            vTaskDelay(1);
#endif
        }
        FLEXIMG_ASSERT(entryPtrs_[i] != nullptr, "transferFrom: NULL entry in this");
        FLEXIMG_ASSERT(entryPtrs_[i]->inUse, "transferFrom: entry not in use (this)");
        FLEXIMG_ASSERT(entryPtrs_[i]->buffer.isValid(), "transferFrom: invalid buffer (this)");
    }
    for (int i = 0; i < source.entryCount_; ++i) {
        FLEXIMG_ASSERT(source.entryPtrs_[i] != nullptr, "transferFrom: NULL entry in source");
        FLEXIMG_ASSERT(source.entryPtrs_[i]->inUse, "transferFrom: entry not in use (source)");
        FLEXIMG_ASSERT(source.entryPtrs_[i]->buffer.isValid(), "transferFrom: invalid buffer (source)");
    }
#endif

    if (source.entryCount_ == 0) {
        return true;  // 空なら何もしない
    }

    // オフセット適用
    source.applyOffset(offsetX);

    // 空の場合: 直接コピー
    if (entryCount_ == 0) {
        for (int i = 0; i < source.entryCount_; ++i) {
            entryPtrs_[i] = source.entryPtrs_[i];
            source.entryPtrs_[i] = nullptr;
        }
        entryCount_ = source.entryCount_;
        source.entryCount_ = 0;
        return true;
    }

    // 既存エントリあり: 1つずつ挿入（重複処理含む）
    for (int i = 0; i < source.entryCount_; ++i) {
        Entry* entry = source.entryPtrs_[i];
        source.entryPtrs_[i] = nullptr;

        // 重複チェック
        int overlapStart = 0, overlapEnd = 0;
        if (findOverlapping(entry->range, overlapStart, overlapEnd)) {
            // 重複あり → 合成処理
            if (!mergeOverlapping(entry, overlapStart, overlapEnd)) {
                return false;
            }
        } else {
            // 重複なし → ソート位置に挿入
            if (!insertSorted(entry)) {
                return false;
            }
        }
    }

    source.entryCount_ = 0;
    return true;
}

// ----------------------------------------------------------------------------
// addBuffer
// ----------------------------------------------------------------------------

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
#ifdef FLEXIMG_DEBUG
    FLEXIMG_ASSERT(entry != nullptr, "insertSorted: entry is null");
    FLEXIMG_ASSERT(entryCount_ >= 0 && entryCount_ <= MAX_ENTRIES, "insertSorted: invalid entryCount_");
    for (int i = 0; i < entryCount_; ++i) {
        FLEXIMG_ASSERT(entryPtrs_[i] != nullptr, "insertSorted: NULL entry in entryPtrs_");
    }
#endif
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
#ifdef FLEXIMG_DEBUG
    FLEXIMG_ASSERT(entryCount_ >= 0 && entryCount_ <= MAX_ENTRIES, "findOverlapping: invalid entryCount_");
#endif
    outOverlapStart = -1;
    outOverlapEnd = -1;

    for (int i = 0; i < entryCount_; ++i) {
        FLEXIMG_ASSERT(entryPtrs_[i], "NULL entry pointer in findOverlapping");
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
//
// 最適化版: 領域分割による効率的な合成
//
// 既存エントリ同士は重複しない（ImageBufferSetの設計による保証）ため、
// 結果バッファの各ピクセルは「新のみ」「既存のみ」「両方（重複）」のいずれか。
//
// 処理:
// 1. 全既存エントリをmergedBufに直接コピー/変換
// 2. 新エントリの非重複部分をmergedBufに直接コピー/変換
// 3. 重複部分のみblendUnder
//
// これにより:
// - ゼロ初期化が不要
// - 一時バッファが不要
// - blendUnderは実際に重複するピクセルのみに適用

bool ImageBufferSet::mergeOverlapping(Entry* newEntry,
                                      int overlapStart, int overlapEnd) {
#ifdef FLEXIMG_DEBUG
    // デバッグ: 引数と内部状態の検証
    FLEXIMG_ASSERT(newEntry != nullptr, "mergeOverlapping: newEntry is null");
    FLEXIMG_ASSERT(newEntry->inUse, "mergeOverlapping: newEntry not in use");
    FLEXIMG_ASSERT(newEntry->buffer.isValid(), "mergeOverlapping: newEntry buffer invalid");
    FLEXIMG_ASSERT(overlapStart >= 0, "mergeOverlapping: overlapStart < 0");
    FLEXIMG_ASSERT(overlapEnd <= entryCount_, "mergeOverlapping: overlapEnd > entryCount_");
    FLEXIMG_ASSERT(overlapStart < overlapEnd, "mergeOverlapping: empty overlap range");
    for (int i = 0; i < entryCount_; ++i) {
        FLEXIMG_ASSERT(entryPtrs_[i] != nullptr, "mergeOverlapping: NULL entry in entryPtrs_");
        FLEXIMG_ASSERT(entryPtrs_[i]->inUse, "mergeOverlapping: entry not in use");
        FLEXIMG_ASSERT(entryPtrs_[i]->buffer.isValid(), "mergeOverlapping: buffer invalid");
    }
#endif

    if (!allocator_) {
        // アロケータがない場合は暫定的に追加
        return insertSorted(newEntry);
    }

    const DataRange& newRange = newEntry->range;
    constexpr size_t bytesPerPixel = 4;

    // 合成対象の全体範囲を計算
    int16_t mergedStartX = newRange.startX;
    int16_t mergedEndX = newRange.endX;
    for (int i = overlapStart; i < overlapEnd; ++i) {
        mergedStartX = std::min(mergedStartX, entryPtrs_[i]->range.startX);
        mergedEndX = std::max(mergedEndX, entryPtrs_[i]->range.endX);
    }
    int mergedWidth = mergedEndX - mergedStartX;

    // 合成用バッファを確保（ゼロ初期化不要）
    ImageBuffer mergedBuf(mergedWidth, 1, PixelFormatIDs::RGBA8_Straight,
                          InitPolicy::Uninitialized, allocator_);
    if (!mergedBuf.isValid()) {
        return insertSorted(newEntry);
    }

    uint8_t* mergedRow = static_cast<uint8_t*>(mergedBuf.view().pixelAt(0, 0));

    // 新エントリのソース情報を取得
    PixelFormatID newFmt = newEntry->buffer.view().formatID;
    const uint8_t* newSrcRow = static_cast<const uint8_t*>(newEntry->buffer.view().pixelAt(0, 0));

    // --- 1. 全既存エントリをmergedBufに直接コピー/変換 ---
    for (int i = overlapStart; i < overlapEnd; ++i) {
        FLEXIMG_ASSERT(entryPtrs_[i], "NULL entry pointer in mergeOverlapping");
        Entry* existing = entryPtrs_[i];
        int16_t exStart = existing->range.startX;
        int16_t exEnd = existing->range.endX;
        int exWidth = exEnd - exStart;
        int dstOffset = exStart - mergedStartX;

        PixelFormatID exFmt = existing->buffer.view().formatID;
        const uint8_t* exSrcRow = static_cast<const uint8_t*>(existing->buffer.view().pixelAt(0, 0));
        uint8_t* dstPtr = mergedRow + static_cast<size_t>(dstOffset) * bytesPerPixel;

        if (exFmt == PixelFormatIDs::RGBA8_Straight) {
            // 同一フォーマット: 直接コピー
            std::memcpy(dstPtr, exSrcRow, static_cast<size_t>(exWidth) * bytesPerPixel);
        } else if (exFmt->toStraight) {
            // フォーマット変換してmergedBufに直接出力
            exFmt->toStraight(dstPtr, exSrcRow, exWidth, &existing->buffer.auxInfo());
        }
    }

    // --- 2. 新エントリの非重複部分をmergedBufに直接コピー/変換 ---

    // ヘルパー: 新エントリの指定範囲をmergedBufにコピー/変換
    const PixelAuxInfo& newAuxInfo = newEntry->buffer.auxInfo();
    auto copyNewRegion = [&](int16_t regionStart, int16_t regionEnd) {
        if (regionStart >= regionEnd) return;
        int width = regionEnd - regionStart;
        int dstOffset = regionStart - mergedStartX;
        int srcOffset = regionStart - newRange.startX;
        uint8_t* dstPtr = mergedRow + static_cast<size_t>(dstOffset) * bytesPerPixel;
        const uint8_t* srcPtr = newSrcRow + static_cast<size_t>(srcOffset) * (newFmt->bytesPerUnit / newFmt->pixelsPerUnit);

        if (newFmt == PixelFormatIDs::RGBA8_Straight) {
            std::memcpy(dstPtr, srcPtr, static_cast<size_t>(width) * bytesPerPixel);
        } else if (newFmt->toStraight) {
            newFmt->toStraight(dstPtr, srcPtr, width, &newAuxInfo);
        }
    };

    // 最初の既存より左
    FLEXIMG_ASSERT(entryPtrs_[overlapStart], "NULL entry at overlapStart");
    int16_t firstExStart = entryPtrs_[overlapStart]->range.startX;
    copyNewRegion(newRange.startX, std::min(newRange.endX, firstExStart));

    // 既存間のギャップ
    for (int i = overlapStart; i < overlapEnd - 1; ++i) {
        FLEXIMG_ASSERT(entryPtrs_[i], "NULL entry in gap loop");
        FLEXIMG_ASSERT(entryPtrs_[i + 1], "NULL entry+1 in gap loop");
        int16_t gapStart = entryPtrs_[i]->range.endX;
        int16_t gapEnd = entryPtrs_[i + 1]->range.startX;
        if (gapStart < gapEnd) {
            int16_t copyStart = std::max(gapStart, newRange.startX);
            int16_t copyEnd = std::min(gapEnd, newRange.endX);
            copyNewRegion(copyStart, copyEnd);
        }
    }

    // 最後の既存より右
    FLEXIMG_ASSERT(entryPtrs_[overlapEnd - 1], "NULL entry at overlapEnd-1");
    int16_t lastExEnd = entryPtrs_[overlapEnd - 1]->range.endX;
    copyNewRegion(std::max(newRange.startX, lastExEnd), newRange.endX);

    // --- 3. 重複部分のみblendUnder ---
    for (int i = overlapStart; i < overlapEnd; ++i) {
        FLEXIMG_ASSERT(entryPtrs_[i], "NULL entry in blendUnder loop");
        Entry* existing = entryPtrs_[i];
        int16_t exStart = existing->range.startX;
        int16_t exEnd = existing->range.endX;

        // 重複範囲を計算
        int16_t oStart = std::max(exStart, newRange.startX);
        int16_t oEnd = std::min(exEnd, newRange.endX);

        if (oStart < oEnd) {
            int width = oEnd - oStart;
            int dstOffset = oStart - mergedStartX;
            int srcOffset = oStart - newRange.startX;
            uint8_t* dstPtr = mergedRow + static_cast<size_t>(dstOffset) * bytesPerPixel;

            // 新エントリをunder合成（既存の下に）
            if (newFmt == PixelFormatIDs::RGBA8_Straight) {
                // 同一フォーマット: 直接blend
                if (newFmt->blendUnderStraight) {
                    newFmt->blendUnderStraight(dstPtr,
                        newSrcRow + static_cast<size_t>(srcOffset) * bytesPerPixel,
                        width, nullptr);
                }
            } else if (newFmt->blendUnderStraight) {
                // 他フォーマットでも直接blend可能なら使用
                const uint8_t* srcPtr = newSrcRow + static_cast<size_t>(srcOffset) * (newFmt->bytesPerUnit / newFmt->pixelsPerUnit);
                newFmt->blendUnderStraight(dstPtr, srcPtr, width, nullptr);
            } else if (newFmt->toStraight) {
                // 変換が必要な場合のみ一時バッファを使用
                ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                                    InitPolicy::Uninitialized, allocator_);
                if (tempBuf.isValid()) {
                    const uint8_t* srcPtr = newSrcRow + static_cast<size_t>(srcOffset) * (newFmt->bytesPerUnit / newFmt->pixelsPerUnit);
                    newFmt->toStraight(tempBuf.view().pixelAt(0, 0), srcPtr, width, &newAuxInfo);
                    PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
                        dstPtr, tempBuf.view().pixelAt(0, 0), width, nullptr);
                }
            }
        }
    }

    // --- エントリ整理 ---

    // 最初の重複エントリを再利用（プール取得の失敗を回避）
    Entry* resultEntry = entryPtrs_[overlapStart];

    // 新エントリを解放（合成済み）
    releaseEntry(newEntry);

    // 最初以外の古いエントリを削除（プールに返却）
    for (int i = overlapStart + 1; i < overlapEnd; ++i) {
        releaseEntry(entryPtrs_[i]);
    }

    // ポインタ配列を詰める（最初のエントリは残す）
    int removeCount = overlapEnd - overlapStart - 1;  // 最初のエントリは残すので -1
    if (removeCount > 0) {
        for (int i = overlapStart + 1; i < entryCount_ - removeCount; ++i) {
            entryPtrs_[i] = entryPtrs_[i + removeCount];
        }
        entryCount_ -= removeCount;
        // 末尾のポインタをNULLクリア（再利用時の安全性確保）
        for (int i = entryCount_; i < entryCount_ + removeCount; ++i) {
            entryPtrs_[i] = nullptr;
        }
    }

    // 結果を最初のエントリに格納
    resultEntry->buffer = std::move(mergedBuf);
    resultEntry->range = DataRange{mergedStartX, mergedEndX};

    return true;  // 既にソート位置にあるので insertSorted 不要
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
        const PixelAuxInfo* auxInfo = &entry->buffer.auxInfo();

        // フォーマット変換
        if (format == PixelFormatIDs::RGBA8_Straight && srcFmt->toStraight) {
            srcFmt->toStraight(dstRow, srcRow, width, auxInfo);
        } else if (srcFmt == PixelFormatIDs::RGBA8_Straight && format->fromStraight) {
            format->fromStraight(dstRow, srcRow, width, auxInfo);
        } else {
            // 汎用変換（Straight経由）
            ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                                InitPolicy::Uninitialized, allocator_);
            if (tempBuf.isValid() && srcFmt->toStraight && format->fromStraight) {
                srcFmt->toStraight(tempBuf.view().pixelAt(0, 0), srcRow, width, auxInfo);
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
        const PixelAuxInfo* auxInfo = &entry->buffer.auxInfo();

        if (srcFmt == format) {
            // 同一フォーマット: 直接コピー
            std::memcpy(dstPtr, srcRow, static_cast<size_t>(width) * dstBpp);
        } else if (format == PixelFormatIDs::RGBA8_Straight && srcFmt->toStraight) {
            // RGBA8_Straightへ変換
            srcFmt->toStraight(dstPtr, srcRow, width, auxInfo);
        } else if (srcFmt == PixelFormatIDs::RGBA8_Straight && format->fromStraight) {
            // RGBA8_Straightから変換
            format->fromStraight(dstPtr, srcRow, width, auxInfo);
        } else {
            // 汎用変換（Straight経由）
            ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                                InitPolicy::Uninitialized, allocator_);
            if (tempBuf.isValid() && srcFmt->toStraight && format->fromStraight) {
                srcFmt->toStraight(tempBuf.view().pixelAt(0, 0), srcRow, width, auxInfo);
                format->fromStraight(dstPtr, tempBuf.view().pixelAt(0, 0), width, nullptr);
            }
        }
    }

    releaseAllEntries();
    return result;
}

// ----------------------------------------------------------------------------
// consolidateInPlace
// ----------------------------------------------------------------------------
//
// 最適化版: ギャップ部分のみゼロ埋め
//
// エントリはソート済み・重複なしのため、以下の構造:
//   [gap?][entry0][gap?][entry1][gap?]...[entryN][gap?]
//
// 処理:
// 1. InitPolicy::Uninitialized でバッファ確保（ゼロ初期化なし）
// 2. 各エントリをコピーしつつ、ギャップ部分のみゼロ埋め
//
// これにより:
// - ギャップなしの場合: ゼロ初期化が完全に不要
// - ギャップありの場合: ギャップ部分のみゼロ埋め

void ImageBufferSet::consolidateInPlace() {
    // 空の場合は何もしない
    if (entryCount_ == 0) {
        return;
    }

    // 単一エントリの場合: rangeのみ正規化（バッファは変更不要）
    // consolidateIfNeeded() が origin.x += range.startX を行うため、
    // range を {0, width} に正規化しないと二重加算が発生する
    if (entryCount_ == 1) {
        int16_t width = static_cast<int16_t>(
            entryPtrs_[0]->range.endX - entryPtrs_[0]->range.startX);
        entryPtrs_[0]->range = DataRange{0, width};
        return;
    }

    // アロケータがない場合は統合できない
    if (!allocator_) {
        return;
    }

    // 全体範囲を計算
    DataRange total = totalRange();
    int totalWidth = total.endX - total.startX;

    if (totalWidth <= 0) {
        return;
    }

    // 統合用バッファを確保（ゼロ初期化なし）
    ImageBuffer merged(totalWidth, 1, PixelFormatIDs::RGBA8_Straight,
                       InitPolicy::Uninitialized, allocator_);
    if (!merged.isValid()) {
        return;
    }

    uint8_t* dstRow = static_cast<uint8_t*>(merged.view().pixelAt(0, 0));
    constexpr size_t bytesPerPixel = 4;

    // 現在の書き込み位置（total.startXからの相対オフセット）
    int cursor = 0;

    // 各エントリを統合バッファにコピー（ギャップをゼロ埋め）
    for (int i = 0; i < entryCount_; ++i) {
        Entry* entry = entryPtrs_[i];
        // 防御的チェック: null エントリや無効なバッファをスキップ
        if (!entry || !entry->buffer.isValid()) {
            continue;
        }

        int entryStart = entry->range.startX - total.startX;
        int entryEnd = entry->range.endX - total.startX;
        int width = entryEnd - entryStart;

        // 防御的チェック: 不正な範囲をスキップ
        if (width <= 0 || entryStart < 0 || entryEnd > totalWidth) {
            continue;
        }

        // ギャップをゼロ埋め（cursor < entryStart の場合）
        if (cursor < entryStart) {
            int gapWidth = entryStart - cursor;
            std::memset(dstRow + static_cast<size_t>(cursor) * bytesPerPixel,
                        0, static_cast<size_t>(gapWidth) * bytesPerPixel);
        }

        // エントリをコピー
        PixelFormatID srcFmt = entry->buffer.view().formatID;
        const void* srcRow = entry->buffer.view().pixelAt(0, 0);
        if (srcRow) {
            void* dstPtr = dstRow + static_cast<size_t>(entryStart) * bytesPerPixel;

            if (srcFmt == PixelFormatIDs::RGBA8_Straight) {
                // 同一フォーマット: 直接コピー
                std::memcpy(dstPtr, srcRow, static_cast<size_t>(width) * bytesPerPixel);
            } else if (srcFmt && srcFmt->toStraight) {
                // RGBA8_Straightへ変換
                srcFmt->toStraight(dstPtr, srcRow, width, &entry->buffer.auxInfo());
            }
        }

        // カーソルを更新
        cursor = entryEnd;
    }

    // 末尾ギャップをゼロ埋め（cursor < totalWidth の場合）
    if (cursor < totalWidth) {
        int gapWidth = totalWidth - cursor;
        std::memset(dstRow + static_cast<size_t>(cursor) * bytesPerPixel,
                    0, static_cast<size_t>(gapWidth) * bytesPerPixel);
    }

    // 最初のエントリを再利用し、残りを解放
    Entry* firstEntry = entryPtrs_[0];
    for (int i = 1; i < entryCount_; ++i) {
        releaseEntry(entryPtrs_[i]);
        entryPtrs_[i] = nullptr;
    }

    // 最初のエントリに統合結果を格納
    firstEntry->buffer = std::move(merged);
    firstEntry->range = DataRange{0, static_cast<int16_t>(totalWidth)};
    entryCount_ = 1;
}

// ----------------------------------------------------------------------------
// replaceBuffer
// ----------------------------------------------------------------------------

void ImageBufferSet::replaceBuffer(int index, ImageBuffer&& buffer) {
    FLEXIMG_ASSERT(index >= 0 && index < entryCount_, "Invalid index");
    entryPtrs_[index]->buffer = std::move(buffer);
    entryPtrs_[index]->range = DataRange{0, static_cast<int16_t>(entryPtrs_[index]->buffer.width())};
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
                    srcFmt->toStraight(mergedRow, srcRow, width, &prev->buffer.auxInfo());
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
                    srcFmt->toStraight(dstPtr, srcRow, width, &curr->buffer.auxInfo());
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
