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
    ImageBufferSet() = default;

    /// @brief コンストラクタ（プール指定）
    /// @param pool エントリプール
    /// @param allocator メモリアロケータ（合成用バッファ確保）
    explicit ImageBufferSet(ImageBufferEntryPool* pool,
                           core::memory::IAllocator* allocator = nullptr)
        : pool_(pool), allocator_(allocator) {}

    /// @brief コンストラクタ（アロケータのみ、後方互換）
    /// @param allocator メモリアロケータ
    explicit ImageBufferSet(core::memory::IAllocator* allocator)
        : allocator_(allocator) {}

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

    /// @brief 新しいバッファを直接作成して登録
    /// @param width バッファ幅
    /// @param height バッファ高さ
    /// @param format ピクセルフォーマット
    /// @param policy 初期化ポリシー
    /// @param startX 開始X座標
    /// @return 作成されたバッファへの参照（失敗時はnullバッファ）
    /// @note ムーブ操作なしでEntry内に直接構築
    ImageBuffer* createBuffer(int width, int height, PixelFormatID format,
                              InitPolicy policy, int16_t startX);

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
        const auto& buf = entryPtrs_[index]->buffer;
        return DataRange{buf.startX(), buf.endX()};
    }

    /// @brief 全体の範囲（最小startX〜最大endX）を取得
    DataRange totalRange() const {
        if (entryCount_ == 0) {
            return DataRange{0, 0};
        }
        return DataRange{
            entryPtrs_[0]->buffer.startX(),
            entryPtrs_[entryCount_ - 1]->buffer.endX()
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
    Entry* entryPtrs_[MAX_ENTRIES] = {};  ///< エントリポインタ配列（ソート済み、nullptrで初期化）
    ImageBufferEntryPool* pool_ = nullptr;     ///< エントリプール
    core::memory::IAllocator* allocator_ = nullptr;  ///< メモリアロケータ（合成用）
    int entryCount_ = 0;  ///< 有効エントリ数

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

    /// @brief エントリを挿入またはマージ（上限超過時は統合して再試行）
    /// @param entry 挿入するエントリ
    /// @return 成功時 true
    /// @note addBuffer/transferFromの共通処理
    bool insertOrMerge(Entry* entry);
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
// フォーマット変換ヘルパー
// ----------------------------------------------------------------------------

/// @brief ソースバッファからRGBA8_Straightバッファへコピー/変換
/// @param dst 出力先（RGBA8_Straight形式、4バイト/ピクセル）
/// @param src 入力元
/// @param width ピクセル数
/// @param srcFmt ソースのピクセルフォーマット
/// @param auxInfo ソースの補助情報（パレット等）
/// @note srcFmtがRGBA8_Straightの場合は直接コピー、それ以外はtoStraightで変換
inline void copyLineToStraight(
    void* dst,
    const void* src,
    int width,
    PixelFormatID srcFmt,
    const PixelAuxInfo* auxInfo)
{
    constexpr size_t bytesPerPixel = 4;
    if (srcFmt == PixelFormatIDs::RGBA8_Straight) {
        std::memcpy(dst, src, static_cast<size_t>(width) * bytesPerPixel);
    } else if (srcFmt && srcFmt->toStraight) {
        srcFmt->toStraight(dst, src, width, auxInfo);
    }
}

/// @brief 任意フォーマット間の変換（Straight経由の汎用変換対応）
/// @param dst 出力先
/// @param src 入力元
/// @param width ピクセル数
/// @param srcFmt ソースのピクセルフォーマット
/// @param dstFmt 出力のピクセルフォーマット
/// @param auxInfo ソースの補助情報（パレット等）
/// @param allocator 一時バッファ用アロケータ（汎用変換時に必要）
/// @note 変換パターン: 同一フォーマット→コピー、→Straight、Straight→、汎用(経由)
inline void convertLine(
    void* dst,
    const void* src,
    int width,
    PixelFormatID srcFmt,
    PixelFormatID dstFmt,
    const PixelAuxInfo* auxInfo,
    core::memory::IAllocator* allocator)
{
    if (srcFmt == dstFmt) {
        // 同一フォーマット: 直接コピー
        size_t bpp = static_cast<size_t>(getBytesPerPixel(dstFmt));
        std::memcpy(dst, src, static_cast<size_t>(width) * bpp);
        return;
    }
    if (dstFmt == PixelFormatIDs::RGBA8_Straight && srcFmt && srcFmt->toStraight) {
        // RGBA8_Straightへ変換
        srcFmt->toStraight(dst, src, width, auxInfo);
        return;
    }
    if (srcFmt == PixelFormatIDs::RGBA8_Straight && dstFmt && dstFmt->fromStraight) {
        // RGBA8_Straightから変換
        dstFmt->fromStraight(dst, src, width, auxInfo);
        return;
    }
    // 汎用変換（Straight経由）
    if (allocator && srcFmt && srcFmt->toStraight && dstFmt && dstFmt->fromStraight) {
        ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                            InitPolicy::Uninitialized, allocator);
        if (tempBuf.isValid()) {
            srcFmt->toStraight(tempBuf.view().pixelAt(0, 0), src, width, auxInfo);
            dstFmt->fromStraight(dst, tempBuf.view().pixelAt(0, 0), width, nullptr);
        }
    }
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
        entry->buffer.addOffset(offsetX);
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

        // 挿入またはマージ
        if (!insertOrMerge(entry)) {
            return false;
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
    entry->buffer.setStartX(range.startX);

    // 空の場合は直接追加
    if (entryCount_ == 0) {
        entryPtrs_[0] = entry;
        entryCount_ = 1;
        return true;
    }

    // 挿入またはマージ
    return insertOrMerge(entry);
}

// ----------------------------------------------------------------------------
// createBuffer
// ----------------------------------------------------------------------------

ImageBuffer* ImageBufferSet::createBuffer(int width, int height, PixelFormatID format,
                                          InitPolicy policy, int16_t startX) {
    if (width <= 0 || height <= 0 || !format) {
        return nullptr;
    }

    // エントリを取得
    Entry* entry = acquireEntry();
    if (!entry) {
        // プール枯渇時: consolidateして空きを作る
        if (entryCount_ > 0) {
            mergeAdjacent(0);
            entry = acquireEntry();
        }
        if (!entry) {
            return nullptr;
        }
    }

    // Entry内に直接ImageBufferを構築（ムーブなし）
    entry->buffer = ImageBuffer(width, height, format, policy, allocator_);
    if (!entry->buffer.isValid()) {
        releaseEntry(entry);
        return nullptr;
    }

    entry->buffer.setStartX(startX);

    // 空の場合は直接追加
    if (entryCount_ == 0) {
        entryPtrs_[0] = entry;
        entryCount_ = 1;
        return &entry->buffer;
    }

    // 挿入またはマージ
    if (!insertOrMerge(entry)) {
        return nullptr;
    }

    return &entry->buffer;
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
        // 上限超過 → 呼び出し元で統合処理を行う
        return false;
    }

    int16_t newStartX = entry->buffer.startX();

    // 挿入位置を探す（startXの昇順）
    int insertPos = 0;
    while (insertPos < entryCount_ && entryPtrs_[insertPos]->buffer.startX() < newStartX) {
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
// insertOrMerge
// ----------------------------------------------------------------------------

bool ImageBufferSet::insertOrMerge(Entry* entry) {
    // エントリ数が半分を超えたら積極的に統合して空きを確保
    if (entryCount_ > (MAX_ENTRIES >> 1)) {
        mergeAdjacent(0);
        if (entryCount_ >= MAX_ENTRIES) {
            // consolidateInPlace前にオフセットを保存
            int16_t originalStartX = entryPtrs_[0]->buffer.startX();
            consolidateInPlace();
            // consolidateInPlaceはstartX=0に正規化するのでオフセットを復元
            if (entryCount_ == 1) {
                entryPtrs_[0]->buffer.addOffset(originalStartX);
            }
        }
    }

    // 重複チェック → マージまたは挿入
    int overlapStart = 0, overlapEnd = 0;
    DataRange entryRange{entry->buffer.startX(), entry->buffer.endX()};
    if (findOverlapping(entryRange, overlapStart, overlapEnd)) {
        return mergeOverlapping(entry, overlapStart, overlapEnd);
    }
    return insertSorted(entry);
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
        const auto& buf = entryPtrs_[i]->buffer;
        int16_t existingStartX = buf.startX();
        int16_t existingEndX = buf.endX();

        // 重複判定: !(range.endX <= existing.startX || range.startX >= existing.endX)
        if (range.endX > existingStartX && range.startX < existingEndX) {
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

    int16_t newStartX = newEntry->buffer.startX();
    int16_t newEndX = newEntry->buffer.endX();
    constexpr size_t bytesPerPixel = 4;

    // ========================================
    // 最適化パス: 既存バッファへの直接ブレンド
    // ========================================
    // 条件:
    // 1. 全既存エントリが直接編集可能（RGBA8_Straight + ownsMemory）
    // 2. 非重複領域が1箇所以下
    // 効果: 新規バッファ確保なしでブレンド完了

    // 条件1: 全既存エントリが直接編集可能か確認
    bool allEditable = true;
    for (int i = overlapStart; i < overlapEnd; ++i) {
        const ImageBuffer& buf = entryPtrs_[i]->buffer;
        if (buf.view().formatID != PixelFormatIDs::RGBA8_Straight || !buf.ownsMemory()) {
            allEditable = false;
            break;
        }
    }

    if (allEditable) {
        // 条件2: 非重複領域をカウント
        int16_t firstExStart = entryPtrs_[overlapStart]->buffer.startX();
        int16_t lastExEnd = entryPtrs_[overlapEnd - 1]->buffer.endX();

        int nonOverlapCount = 0;

        // 左側の非重複（新エントリが既存より左にはみ出す）
        if (newStartX < firstExStart) {
            ++nonOverlapCount;
        }

        // 既存エントリ間のギャップで新エントリがカバーする部分
        for (int i = overlapStart; i < overlapEnd - 1; ++i) {
            int16_t gapStart = entryPtrs_[i]->buffer.endX();
            int16_t gapEnd = entryPtrs_[i + 1]->buffer.startX();
            if (gapStart < gapEnd) {
                // ギャップが存在し、新エントリがカバーしている
                int16_t coverStart = std::max(gapStart, newStartX);
                int16_t coverEnd = std::min(gapEnd, newEndX);
                if (coverStart < coverEnd) {
                    ++nonOverlapCount;
                }
            }
        }

        // 右側の非重複（新エントリが既存より右にはみ出す）
        if (newEndX > lastExEnd) {
            ++nonOverlapCount;
        }

        // 条件を満たす場合: 直接ブレンドパス
        // Phase 1: 非重複領域が0の場合のみ（完全内包ケース）
        // 将来的にnonOverlapCount == 1のケースも対応予定
        if (nonOverlapCount == 0) {
            PixelFormatID newFmt = newEntry->buffer.view().formatID;
            const PixelAuxInfo* newAuxInfo = &newEntry->buffer.auxInfo();

            // 各既存エントリに対して重複部分をブレンド
            for (int i = overlapStart; i < overlapEnd; ++i) {
                Entry* existing = entryPtrs_[i];
                int16_t exStart = existing->buffer.startX();
                int16_t exEnd = existing->buffer.endX();

                // 重複範囲を計算
                int16_t oStart = std::max(exStart, newStartX);
                int16_t oEnd = std::min(exEnd, newEndX);

                if (oStart < oEnd) {
                    int width = oEnd - oStart;
                    int dstOffset = oStart - exStart;
                    int srcOffset = oStart - newStartX;

                    uint8_t* dstPtr = static_cast<uint8_t*>(existing->buffer.view().pixelAt(dstOffset, 0));
                    size_t srcBpp = newFmt->bytesPerUnit / newFmt->pixelsPerUnit;
                    const uint8_t* srcPtr = static_cast<const uint8_t*>(
                        newEntry->buffer.view().pixelAt(0, 0)) + static_cast<size_t>(srcOffset) * srcBpp;

                    // 新エントリをunder合成（既存の下に）
                    if (newFmt == PixelFormatIDs::RGBA8_Straight) {
                        if (newFmt->blendUnderStraight) {
                            newFmt->blendUnderStraight(dstPtr, srcPtr, width, newAuxInfo);
                        }
                    } else if (newFmt->blendUnderStraight) {
                        newFmt->blendUnderStraight(dstPtr, srcPtr, width, newAuxInfo);
                    } else if (newFmt->toStraight && allocator_) {
                        // 変換が必要な場合は一時バッファを使用
                        ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                                            InitPolicy::Uninitialized, allocator_);
                        if (tempBuf.isValid()) {
                            newFmt->toStraight(tempBuf.view().pixelAt(0, 0), srcPtr, width, newAuxInfo);
                            PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
                                dstPtr, tempBuf.view().pixelAt(0, 0), width, nullptr);
                        }
                    }
                }
            }

            // 非重複領域なし: 新エントリを解放して完了
            releaseEntry(newEntry);
            return true;
        }
    }

    // ========================================
    // 通常パス: 新規バッファを確保してマージ
    // ========================================

    // 合成対象の全体範囲を計算
    int16_t mergedStartX = newStartX;
    int16_t mergedEndX = newEndX;
    for (int i = overlapStart; i < overlapEnd; ++i) {
        mergedStartX = std::min(mergedStartX, entryPtrs_[i]->buffer.startX());
        mergedEndX = std::max(mergedEndX, entryPtrs_[i]->buffer.endX());
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
        int16_t exStart = existing->buffer.startX();
        int16_t exEnd = existing->buffer.endX();
        int exWidth = exEnd - exStart;
        int dstOffset = exStart - mergedStartX;

        uint8_t* dstPtr = mergedRow + static_cast<size_t>(dstOffset) * bytesPerPixel;
        copyLineToStraight(dstPtr, existing->buffer.view().pixelAt(0, 0),
                           exWidth, existing->buffer.view().formatID,
                           &existing->buffer.auxInfo());
    }

    // --- 2. 新エントリの非重複部分をmergedBufに直接コピー/変換 ---

    // ヘルパー: 新エントリの指定範囲をmergedBufにコピー/変換
    const PixelAuxInfo* newAuxInfo = &newEntry->buffer.auxInfo();
    size_t srcBytesPerPixel = newFmt->bytesPerUnit / newFmt->pixelsPerUnit;
    auto copyNewRegion = [&](int16_t regionStart, int16_t regionEnd) {
        if (regionStart >= regionEnd) return;
        int width = regionEnd - regionStart;
        int dstOffset = regionStart - mergedStartX;
        int srcOffset = regionStart - newStartX;
        uint8_t* dstPtr = mergedRow + static_cast<size_t>(dstOffset) * bytesPerPixel;
        const uint8_t* srcPtr = newSrcRow + static_cast<size_t>(srcOffset) * srcBytesPerPixel;
        copyLineToStraight(dstPtr, srcPtr, width, newFmt, newAuxInfo);
    };

    // 最初の既存より左
    FLEXIMG_ASSERT(entryPtrs_[overlapStart], "NULL entry at overlapStart");
    int16_t firstExStart = entryPtrs_[overlapStart]->buffer.startX();
    copyNewRegion(newStartX, std::min(newEndX, firstExStart));

    // 既存間のギャップ
    for (int i = overlapStart; i < overlapEnd - 1; ++i) {
        FLEXIMG_ASSERT(entryPtrs_[i], "NULL entry in gap loop");
        FLEXIMG_ASSERT(entryPtrs_[i + 1], "NULL entry+1 in gap loop");
        int16_t gapStart = entryPtrs_[i]->buffer.endX();
        int16_t gapEnd = entryPtrs_[i + 1]->buffer.startX();
        if (gapStart < gapEnd) {
            int16_t copyStart = std::max(gapStart, newStartX);
            int16_t copyEnd = std::min(gapEnd, newEndX);
            copyNewRegion(copyStart, copyEnd);
        }
    }

    // 最後の既存より右
    FLEXIMG_ASSERT(entryPtrs_[overlapEnd - 1], "NULL entry at overlapEnd-1");
    int16_t lastExEnd = entryPtrs_[overlapEnd - 1]->buffer.endX();
    copyNewRegion(std::max(newStartX, lastExEnd), newEndX);

    // --- 3. 重複部分のみblendUnder ---
    for (int i = overlapStart; i < overlapEnd; ++i) {
        FLEXIMG_ASSERT(entryPtrs_[i], "NULL entry in blendUnder loop");
        Entry* existing = entryPtrs_[i];
        int16_t exStart = existing->buffer.startX();
        int16_t exEnd = existing->buffer.endX();

        // 重複範囲を計算
        int16_t oStart = std::max(exStart, newStartX);
        int16_t oEnd = std::min(exEnd, newEndX);

        if (oStart < oEnd) {
            int width = oEnd - oStart;
            int dstOffset = oStart - mergedStartX;
            int srcOffset = oStart - newStartX;
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
                    newFmt->toStraight(tempBuf.view().pixelAt(0, 0), srcPtr, width, newAuxInfo);
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
    resultEntry->buffer.setStartX(mergedStartX);

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

        int width = entry->buffer.width();

        // 変換先バッファを確保
        ImageBuffer converted(width, 1, format, InitPolicy::Uninitialized, allocator_);
        if (!converted.isValid()) {
            continue;
        }

        const void* srcRow = entry->buffer.view().pixelAt(0, 0);
        void* dstRow = converted.view().pixelAt(0, 0);
        const PixelAuxInfo* auxInfo = &entry->buffer.auxInfo();

        // フォーマット変換（convertLineヘルパー使用）
        convertLine(dstRow, srcRow, width, srcFmt, format, auxInfo, allocator_);

        entry->buffer = std::move(converted);
    }
}

// ----------------------------------------------------------------------------
// consolidate
// ----------------------------------------------------------------------------
//
// 最適化版: ギャップ部分のみゼロ埋め
//
// 処理:
// 1. InitPolicy::Uninitialized でバッファ確保（ゼロ初期化なし）
// 2. 各エントリをコピーしつつ、ギャップ部分のみゼロ埋め
//
// これにより:
// - ギャップなしの場合: ゼロ初期化が完全に不要
// - ギャップありの場合: ギャップ部分のみゼロ埋め

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

    // 出力バッファを確保（ゼロ初期化なし）
    ImageBuffer result(totalWidth, 1, format, InitPolicy::Uninitialized, allocator_);
    if (!result.isValid()) {
        ImageBuffer fallback = std::move(entryPtrs_[0]->buffer);
        releaseAllEntries();
        return fallback;
    }

    uint8_t* dstRow = static_cast<uint8_t*>(result.view().pixelAt(0, 0));
    size_t dstBpp = static_cast<size_t>(getBytesPerPixel(format));

    // 現在の書き込み位置（total.startXからの相対オフセット）
    int cursor = 0;

    // 各エントリを出力バッファにコピー（ギャップをゼロ埋め）
    for (int i = 0; i < entryCount_; ++i) {
        Entry* entry = entryPtrs_[i];
        int entryStart = entry->buffer.startX() - total.startX;
        int entryEnd = entry->buffer.endX() - total.startX;
        int width = entryEnd - entryStart;

        // ギャップをゼロ埋め（cursor < entryStart の場合）
        if (cursor < entryStart) {
            int gapWidth = entryStart - cursor;
            std::memset(dstRow + static_cast<size_t>(cursor) * dstBpp,
                        0, static_cast<size_t>(gapWidth) * dstBpp);
        }

        PixelFormatID srcFmt = entry->buffer.view().formatID;
        const void* srcRow = entry->buffer.view().pixelAt(0, 0);
        void* dstPtr = dstRow + static_cast<size_t>(entryStart) * dstBpp;
        const PixelAuxInfo* auxInfo = &entry->buffer.auxInfo();

        // フォーマット変換（convertLineヘルパー使用）
        convertLine(dstPtr, srcRow, width, srcFmt, format, auxInfo, allocator_);

        // カーソルを更新
        cursor = entryEnd;
    }

    // 末尾ギャップをゼロ埋め（cursor < totalWidth の場合）
    if (cursor < totalWidth) {
        int gapWidth = totalWidth - cursor;
        std::memset(dstRow + static_cast<size_t>(cursor) * dstBpp,
                    0, static_cast<size_t>(gapWidth) * dstBpp);
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

    // 単一エントリの場合: startXのみ正規化（バッファは変更不要）
    // consolidateIfNeeded() が origin.x += startX を行うため、
    // startX を 0 に正規化しないと二重加算が発生する
    if (entryCount_ == 1) {
        entryPtrs_[0]->buffer.setStartX(0);
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

        int entryStart = entry->buffer.startX() - total.startX;
        int entryEnd = entry->buffer.endX() - total.startX;
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
        const void* srcRow = entry->buffer.view().pixelAt(0, 0);
        if (srcRow) {
            void* dstPtr = dstRow + static_cast<size_t>(entryStart) * bytesPerPixel;
            copyLineToStraight(dstPtr, srcRow, width,
                               entry->buffer.view().formatID,
                               &entry->buffer.auxInfo());
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
    firstEntry->buffer.setStartX(0);
    entryCount_ = 1;
}

// ----------------------------------------------------------------------------
// replaceBuffer
// ----------------------------------------------------------------------------

void ImageBufferSet::replaceBuffer(int index, ImageBuffer&& buffer) {
    FLEXIMG_ASSERT(index >= 0 && index < entryCount_, "Invalid index");
    entryPtrs_[index]->buffer = std::move(buffer);
    entryPtrs_[index]->buffer.setStartX(0);
}

// ----------------------------------------------------------------------------
// mergeAdjacent
// ----------------------------------------------------------------------------
//
// 最適化版: ギャップ部分のみゼロ埋め
//
// 処理:
// 1. InitPolicy::Uninitialized でバッファ確保（ゼロ初期化なし）
// 2. prevをコピー
// 3. ギャップ部分のみゼロ埋め（gap > 0 の場合のみ）
// 4. currをコピー
//
// これにより:
// - ギャップなしの場合: ゼロ初期化が完全に不要
// - ギャップありの場合: ギャップ部分のみゼロ埋め

void ImageBufferSet::mergeAdjacent(int16_t gapThreshold) {
    if (entryCount_ < 2 || !allocator_) {
        return;
    }

    // 隣接エントリを統合（後ろから前へ処理して、インデックスのずれを回避）
    for (int i = entryCount_ - 1; i > 0; --i) {
        Entry* curr = entryPtrs_[i];
        Entry* prev = entryPtrs_[i - 1];

        int16_t gap = static_cast<int16_t>(curr->buffer.startX() - prev->buffer.endX());

        if (gap <= gapThreshold) {
            // 統合: prevとcurrをマージ
            int16_t mergedStartX = prev->buffer.startX();
            int16_t mergedEndX = curr->buffer.endX();
            int mergedWidth = mergedEndX - mergedStartX;

            // 統合バッファを確保（ゼロ初期化なし）
            ImageBuffer merged(mergedWidth, 1, PixelFormatIDs::RGBA8_Straight,
                               InitPolicy::Uninitialized, allocator_);
            if (!merged.isValid()) {
                continue;
            }

            uint8_t* mergedRow = static_cast<uint8_t*>(merged.view().pixelAt(0, 0));
            constexpr size_t bytesPerPixel = 4;

            // prev をコピー
            int prevWidth = prev->buffer.width();
            copyLineToStraight(mergedRow, prev->buffer.view().pixelAt(0, 0),
                               prevWidth, prev->buffer.view().formatID,
                               &prev->buffer.auxInfo());

            // ギャップ部分をゼロ埋め（gap > 0 の場合のみ）
            if (gap > 0) {
                uint8_t* gapPtr = mergedRow + static_cast<size_t>(prevWidth) * bytesPerPixel;
                std::memset(gapPtr, 0, static_cast<size_t>(gap) * bytesPerPixel);
            }

            // curr をコピー
            int currWidth = curr->buffer.width();
            int currDstOffset = curr->buffer.startX() - mergedStartX;
            void* currDstPtr = mergedRow + static_cast<size_t>(currDstOffset) * bytesPerPixel;
            copyLineToStraight(currDstPtr, curr->buffer.view().pixelAt(0, 0),
                               currWidth, curr->buffer.view().formatID,
                               &curr->buffer.auxInfo());

            // prevを統合結果で置き換え
            prev->buffer = std::move(merged);
            prev->buffer.setStartX(mergedStartX);

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
