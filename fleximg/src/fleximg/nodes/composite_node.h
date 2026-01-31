#ifndef FLEXIMG_COMPOSITE_NODE_H
#define FLEXIMG_COMPOSITE_NODE_H

#include "../core/node.h"
#include "../core/affine_capability.h"
#include "../core/perf_metrics.h"
#include "../image/image_buffer.h"
#include "../image/pixel_format.h"
#include "../operations/canvas_utils.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// CompositeNode - 合成ノード
// ========================================================================
//
// 複数の入力画像を合成して1つの出力を生成します。
// - 入力: コンストラクタで指定（デフォルト2）
// - 出力: 1ポート
//
// 合成方式:
// - 8bit Straight形式（4バイト/ピクセル）
//
// 合成順序（under合成）:
// - 入力ポート0が最前面（最初に描画）
// - 入力ポート1以降が順に背面に合成
// - 既に不透明なピクセルは後のレイヤー処理をスキップ
//
// アフィン変換はAffineCapability Mixinから継承:
// - setMatrix(), matrix()
// - setRotation(), setScale(), setTranslation(), setRotationScale()
// - 設定した変換は全上流ノードに伝播される
//
// 使用例:
//   CompositeNode composite(3);  // 3入力
//   composite.setRotation(0.5f); // 合成結果全体を回転
//   fg >> composite;             // ポート0（最前面）
//   mid.connectTo(composite, 1); // ポート1（中間）
//   bg.connectTo(composite, 2);  // ポート2（最背面）
//   composite >> sink;
//

class CompositeNode : public Node, public AffineCapability {
public:
    explicit CompositeNode(int_fast16_t inputCount = 2) {
        initPorts(static_cast<int>(inputCount), 1);  // 入力N、出力1
    }

    // ========================================
    // 入力管理
    // ========================================

    // 入力数を変更（既存接続は維持）
    void setInputCount(int_fast16_t count) {
        if (count < 1) count = 1;
        inputs_.resize(static_cast<size_t>(count));
        for (int_fast16_t i = 0; i < count; ++i) {
            if (inputs_[static_cast<size_t>(i)].owner == nullptr) {
                inputs_[static_cast<size_t>(i)] = core::Port(this, static_cast<int>(i));
            }
        }
    }

    int_fast16_t inputCount() const {
        return static_cast<int_fast16_t>(inputs_.size());
    }

    // ========================================
    // Node インターフェース
    // ========================================

    const char* name() const override { return "CompositeNode"; }

    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: 全上流ノードにPrepareRequestを伝播
    PrepareResponse onPullPrepare(const PrepareRequest& request) override;

    // onPullFinalize: 全上流ノードに終了を伝播
    void onPullFinalize() override;

    // onPullProcess: 複数の上流から画像を取得してunder合成
    RenderResponse onPullProcess(const RenderRequest& request) override;

    // getDataRange: 全上流のgetDataRange和集合を返す
    DataRange getDataRange(const RenderRequest& request) const override;

protected:
    int nodeTypeForMetrics() const override { return NodeType::Composite; }

private:
    // 有効な上流のキャッシュエントリ（Node* + DataRange）
    struct UpstreamCacheEntry {
        Node* node;
        DataRange range;
    };

    // 有効な上流のみを格納するキャッシュ（onPullPrepareで確保、onPullFinalizeで解放）
    UpstreamCacheEntry* upstreamCache_ = nullptr;
    int_fast16_t upstreamCacheCapacity_ = 0;  // 確保サイズ（inputCount）
    mutable int_fast16_t validUpstreamCount_ = 0;  // 有効エントリ数（calcUpstreamRangeUnionで設定）

    // 描画済み範囲の配列（onPullProcess内で使用、ソート済みを維持）
    DataRange* drawnRanges_ = nullptr;

    // getDataRange/onPullProcess 間のキャッシュ
    struct DataRangeCache {
        Point origin = {INT32_MIN, INT32_MIN};  // キャッシュキー（無効値で初期化）
        int16_t startX = 0;
        int16_t endX = 0;
        int16_t validUpstreamCount = 0;  // 有効な上流数もキャッシュ
    };
    mutable DataRangeCache rangeCache_;

    // 全上流のgetDataRange和集合を計算し、各上流のDataRangeをキャッシュに保存
    DataRange calcUpstreamRangeUnion(const RenderRequest& request) const;
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// CompositeNode - Template Method フック実装
// ============================================================================

PrepareResponse CompositeNode::onPullPrepare(const PrepareRequest& request) {
    // 有効な上流キャッシュと描画済み範囲配列を確保（最大で入力数分）
    {
        auto cacheInputCount = inputCount();
        if (cacheInputCount > 0 && allocator()) {
            // UpstreamCacheEntry配列
            size_t cacheSize = static_cast<size_t>(cacheInputCount) * sizeof(UpstreamCacheEntry);
            void* mem = allocator()->allocate(cacheSize, alignof(UpstreamCacheEntry));
            if (mem) {
                upstreamCache_ = static_cast<UpstreamCacheEntry*>(mem);
                upstreamCacheCapacity_ = cacheInputCount;
                validUpstreamCount_ = 0;  // calcUpstreamRangeUnionで設定される
            }
            // DataRange配列（描画済み範囲用）
            size_t rangeSize = static_cast<size_t>(cacheInputCount) * sizeof(DataRange);
            void* rangeMem = allocator()->allocate(rangeSize, alignof(DataRange));
            if (rangeMem) {
                drawnRanges_ = static_cast<DataRange*>(rangeMem);
            }
        }
    }

    PrepareResponse merged;
    merged.status = PrepareStatus::Prepared;
    int validUpstreamCount = 0;

    // AABB和集合計算用（基準点からの相対座標）
    float minX = 0, minY = 0, maxX = 0, maxY = 0;

    // 上流に渡すリクエストを作成（localMatrix_ を累積）
    PrepareRequest upstreamRequest = request;
    if (hasLocalTransform()) {
        // 行列合成: request.affineMatrix * localMatrix_
        // AffineNode直列接続と同じ解釈順序
        if (upstreamRequest.hasAffine) {
            upstreamRequest.affineMatrix = upstreamRequest.affineMatrix * localMatrix_;
        } else {
            upstreamRequest.affineMatrix = localMatrix_;
            upstreamRequest.hasAffine = true;
        }
    }

    // 全上流へ伝播し、結果をマージ（AABB和集合）
    auto numInputs = inputCount();
    for (int_fast16_t i = 0; i < numInputs; ++i) {
        Node* upstream = upstreamNode(i);
        if (upstream) {
            // 各上流に同じリクエストを伝播
            // 注意: アフィン行列は共有されるため、各上流で同じ変換が適用される
            PrepareResponse result = upstream->pullPrepare(upstreamRequest);
            if (!result.ok()) {
                return result;  // エラーを伝播
            }

            // 各結果のAABBをワールド座標に変換
            // origin はバッファ左上のワールド座標
            float left = fixed_to_float(result.origin.x);
            float top = fixed_to_float(result.origin.y);
            float right = left + static_cast<float>(result.width);
            float bottom = top + static_cast<float>(result.height);

            if (validUpstreamCount == 0) {
                // 最初の結果でベースを初期化
                merged.preferredFormat = result.preferredFormat;
                minX = left;
                minY = top;
                maxX = right;
                maxY = bottom;
            } else {
                // 和集合（各辺のmin/max）
                if (left < minX) minX = left;
                if (top < minY) minY = top;
                if (right > maxX) maxX = right;
                if (bottom > maxY) maxY = bottom;
            }
            ++validUpstreamCount;
        }
    }

    if (validUpstreamCount > 0) {
        // 和集合結果をPrepareResponseに設定
        // origin はバッファ左上のワールド座標
        merged.width = static_cast<int16_t>(std::ceil(maxX - minX));
        merged.height = static_cast<int16_t>(std::ceil(maxY - minY));
        merged.origin.x = float_to_fixed(minX);
        merged.origin.y = float_to_fixed(minY);

        // フォーマット決定:
        // - 上流が1つのみ → パススルー（merged.preferredFormatはそのまま）
        // - 上流が複数 → 合成フォーマットを使用
        if (validUpstreamCount > 1) {
            merged.preferredFormat = PixelFormatIDs::RGBA8_Straight;
        }
    } else {
        // 上流がない場合はサイズ0を返す
        // width/height/originはデフォルト値（0）のまま
    }

    // 準備処理
    RenderRequest screenInfo;
    screenInfo.width = request.width;
    screenInfo.height = request.height;
    screenInfo.origin = request.origin;
    prepare(screenInfo);

    return merged;
}

void CompositeNode::onPullFinalize() {
    // キャッシュを解放
    if (upstreamCache_ && allocator()) {
        allocator()->deallocate(upstreamCache_);
        upstreamCache_ = nullptr;
        upstreamCacheCapacity_ = 0;
        validUpstreamCount_ = 0;
    }
    if (drawnRanges_ && allocator()) {
        allocator()->deallocate(drawnRanges_);
        drawnRanges_ = nullptr;
    }
    // キャッシュキーも無効化
    rangeCache_.origin = {INT32_MIN, INT32_MIN};

    finalize();
    auto numInputs = inputCount();
    for (int_fast16_t i = 0; i < numInputs; ++i) {
        Node* upstream = upstreamNode(i);
        if (upstream) {
            upstream->pullFinalize();
        }
    }
}

// 全上流のgetDataRange和集合を計算し、有効な上流のみキャッシュに保存
DataRange CompositeNode::calcUpstreamRangeUnion(const RenderRequest& request) const {
    auto numInputs = inputCount();
    int_fast16_t startX = request.width;  // 右端で初期化
    int_fast16_t endX = 0;                // 左端で初期化
    int_fast16_t cacheIndex = 0;          // キャッシュ書き込み位置

    for (int_fast16_t i = 0; i < numInputs; i++) {
        Node* upstream = upstreamNode(i);
        if (!upstream) continue;

        DataRange range = upstream->getDataRange(request);
        if (!range.hasData()) continue;

        // 有効な上流のみキャッシュに追加
        if (upstreamCache_ && cacheIndex < upstreamCacheCapacity_) {
            upstreamCache_[cacheIndex].node = upstream;
            upstreamCache_[cacheIndex].range = range;
            ++cacheIndex;
        }

        // 和集合を更新
        if (range.startX < startX) startX = range.startX;
        if (range.endX > endX) endX = range.endX;
    }

    validUpstreamCount_ = cacheIndex;
    return DataRange{static_cast<int16_t>(startX), static_cast<int16_t>(endX)};  // startX >= endX はデータなし
}

// getDataRange: 全上流のgetDataRange和集合を返す
// 計算結果はキャッシュし、onPullProcessで再利用
DataRange CompositeNode::getDataRange(const RenderRequest& request) const {
    DataRange range = calcUpstreamRangeUnion(request);

    // キャッシュに保存（validUpstreamCount_ は calcUpstreamRangeUnion で設定済み）
    rangeCache_.origin = request.origin;
    rangeCache_.startX = range.startX;
    rangeCache_.endX = range.endX;
    rangeCache_.validUpstreamCount = static_cast<int16_t>(validUpstreamCount_);

    return range.hasData() ? range : DataRange{0, 0};
}

// onPullProcess: 複数の上流から画像を取得してunder合成
// under合成: 手前から奥へ処理し、不透明な部分は後のレイヤーをスキップ
// 最適化: height=1 前提（パイプラインは常にスキャンライン単位で処理）
//
// 描画済み範囲配列方式:
// - drawnRanges_にソート済みの描画済み範囲を保持
// - 各レイヤー描画時に、重複部分はblend、非重複部分はconverter
// - 描画後に範囲をマージして配列を更新
RenderResponse CompositeNode::onPullProcess(const RenderRequest& request) {
    auto numInputs = inputCount();
    if (numInputs == 0) return RenderResponse();

    // キャンバス範囲を取得（キャッシュがあれば再利用）
    int16_t canvasStartX, canvasEndX;
    int16_t cachedValidCount;
    if (rangeCache_.origin.x == request.origin.x &&
        rangeCache_.origin.y == request.origin.y) {
        // キャッシュヒット: getDataRange()の計算結果を再利用
        canvasStartX = rangeCache_.startX;
        canvasEndX = rangeCache_.endX;
        cachedValidCount = rangeCache_.validUpstreamCount;
    } else {
        // キャッシュミス: 和集合を計算
        DataRange range = calcUpstreamRangeUnion(request);
        canvasStartX = range.startX;
        canvasEndX = range.endX;
        cachedValidCount = static_cast<int16_t>(validUpstreamCount_);
    }

    // 有効なデータがない場合は空を返す（originは維持）
    if (canvasStartX >= canvasEndX) {
        return RenderResponse(ImageBuffer(), request.origin);
    }

    // 有効な上流が1件のみの場合、そのまま返す（最適化）
    // キャンバス作成・フォーマット変換・コピー処理をスキップ
    if (cachedValidCount == 1 && upstreamCache_) {
        Node* upstream = upstreamCache_[0].node;
        return upstream->pullProcess(request);
    }

    int16_t canvasWidth = canvasEndX - canvasStartX;

    // キャンバス左上のワールド座標（固定小数点 Q16.16）
    // canvasStartX分だけ右にシフト
    int_fixed canvasOriginX = request.origin.x + to_fixed(canvasStartX);
    int_fixed canvasOriginY = request.origin.y;

    // キャンバスを作成（height=1、必要幅のみ確保）
    // 8bit Straight形式: 4バイト/ピクセル
    constexpr size_t bytesPerPixel = 4;
    PixelFormatID canvasFormat = PixelFormatIDs::RGBA8_Straight;
    ImageBuffer canvasBuf(canvasWidth, 1, canvasFormat,
                          InitPolicy::Uninitialized, allocator());
#ifdef FLEXIMG_DEBUG_PERF_METRICS
    PerfMetrics::instance().nodes[NodeType::Composite].recordAlloc(
        canvasBuf.totalBytes(), canvasBuf.width(), canvasBuf.height());
#endif
    uint8_t* canvasRow = static_cast<uint8_t*>(canvasBuf.view().pixelAt(0, 0));

    // 描画済み範囲配列を初期化
    int_fast16_t drawnCount = 0;
    // drawnRanges_がnullの場合のフォールバック用（単一範囲追跡）
    int16_t fallbackStartX = canvasWidth;
    int16_t fallbackEndX = 0;

    // under合成: 有効な上流のみ順に評価して合成
    // 入力ポート0が最前面、以降が背面（キャッシュは同じ順序で格納）
    for (int_fast16_t i = 0; i < validUpstreamCount_; i++) {
        // キャッシュから取得（hasData()チェック不要、キャッシュに入っている時点で有効）
        Node* upstream = upstreamCache_[i].node;

        // 上流を評価（計測対象外）
        RenderResponse inputResult = upstream->pullProcess(request);

        // 空入力はスキップ
        if (!inputResult.isValid()) continue;

        // ここからCompositeNode自身の処理を計測
        FLEXIMG_METRICS_SCOPE(NodeType::Composite);

        // X方向オフセット計算（Y方向は不要、height=1前提）
        // 入力バッファ左上のワールド座標 - キャンバス左上のワールド座標 = 描画開始位置
        int offsetX = from_fixed(inputResult.origin.x - canvasOriginX);
        int srcStartX = std::max(0, -offsetX);
        int dstStartX = std::max(0, offsetX);
        int copyWidth = std::min(inputResult.view().width - srcStartX,
                                 static_cast<int>(canvasWidth) - dstStartX);
        if (copyWidth <= 0) continue;

        const auto* srcBytes = static_cast<const uint8_t*>(inputResult.view().pixelAt(srcStartX, 0));
        PixelFormatID srcFmt = inputResult.view().formatID;
        size_t srcBpp = static_cast<size_t>(getBytesPerPixel(srcFmt));

        // 入力ごとに変換パスを解決（分岐なしの変換関数を取得）
        auto converter = resolveConverter(
            srcFmt, canvasFormat,
            &inputResult.buffer.auxInfo(), allocator());

        // 今回の描画範囲
        int16_t curStartX = static_cast<int16_t>(dstStartX);
        int16_t curEndX = static_cast<int16_t>(dstStartX + copyWidth);

        // 描画済み範囲と比較して、重複/非重複を処理
        // srcPosは今回の入力内でのオフセット（処理済みピクセル数）
        int srcPos = 0;
        int writePos = curStartX;

        if (drawnRanges_) {
            // drawnRanges_がある場合: 範囲配列と比較
            for (int_fast16_t j = 0; j < drawnCount && writePos < curEndX; j++) {
                DataRange& drawn = drawnRanges_[j];

                // 描画済み範囲より左にある非重複部分 → converter
                if (writePos < drawn.startX) {
                    int nonOverlapEnd = std::min(static_cast<int>(drawn.startX), static_cast<int>(curEndX));
                    int width = nonOverlapEnd - writePos;
                    if (width > 0 && converter) {
                        converter(canvasRow + static_cast<size_t>(writePos) * bytesPerPixel,
                                  srcBytes + static_cast<size_t>(srcPos) * srcBpp, width);
                    }
                    srcPos += width;
                    writePos = nonOverlapEnd;
                }

                // 描画済み範囲との重複部分 → blend
                if (writePos < curEndX && writePos < drawn.endX && drawn.startX < curEndX) {
                    int overlapStart = std::max(writePos, static_cast<int>(drawn.startX));
                    int overlapEnd = std::min(static_cast<int>(curEndX), static_cast<int>(drawn.endX));
                    int width = overlapEnd - overlapStart;
                    if (width > 0) {
                        const uint8_t* overlapSrc = srcBytes + static_cast<size_t>(srcPos) * srcBpp;
                        uint8_t* overlapDst = canvasRow + static_cast<size_t>(overlapStart) * bytesPerPixel;
                        if (srcFmt->blendUnderStraight) {
                            srcFmt->blendUnderStraight(overlapDst, overlapSrc, width, nullptr);
                        } else if (converter) {
                            ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                                                InitPolicy::Uninitialized, allocator());
                            converter(tempBuf.view().pixelAt(0, 0), overlapSrc, width);
                            PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
                                overlapDst, tempBuf.view().pixelAt(0, 0), width, nullptr);
                        } else if (srcFmt->toStraight) {
                            ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                                                InitPolicy::Uninitialized, allocator());
                            srcFmt->toStraight(tempBuf.view().pixelAt(0, 0), overlapSrc, width, nullptr);
                            PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
                                overlapDst, tempBuf.view().pixelAt(0, 0), width, nullptr);
                        }
                        srcPos += width;
                        writePos = overlapEnd;
                    }
                }
            }
        } else if (drawnCount > 0) {
            // フォールバック: 単一範囲（fallbackStartX〜fallbackEndX）と比較
            // 非重複部分（左側） → converter
            if (writePos < fallbackStartX) {
                int nonOverlapEnd = std::min(static_cast<int>(fallbackStartX), static_cast<int>(curEndX));
                int width = nonOverlapEnd - writePos;
                if (width > 0 && converter) {
                    converter(canvasRow + static_cast<size_t>(writePos) * bytesPerPixel,
                              srcBytes + static_cast<size_t>(srcPos) * srcBpp, width);
                }
                srcPos += width;
                writePos = nonOverlapEnd;
            }
            // 重複部分 → blend
            if (writePos < curEndX && writePos < fallbackEndX && fallbackStartX < curEndX) {
                int overlapStart = std::max(writePos, static_cast<int>(fallbackStartX));
                int overlapEnd = std::min(static_cast<int>(curEndX), static_cast<int>(fallbackEndX));
                int width = overlapEnd - overlapStart;
                if (width > 0) {
                    const uint8_t* overlapSrc = srcBytes + static_cast<size_t>(srcPos) * srcBpp;
                    uint8_t* overlapDst = canvasRow + static_cast<size_t>(overlapStart) * bytesPerPixel;
                    if (srcFmt->blendUnderStraight) {
                        srcFmt->blendUnderStraight(overlapDst, overlapSrc, width, nullptr);
                    } else if (converter) {
                        ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                                            InitPolicy::Uninitialized, allocator());
                        converter(tempBuf.view().pixelAt(0, 0), overlapSrc, width);
                        PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
                            overlapDst, tempBuf.view().pixelAt(0, 0), width, nullptr);
                    } else if (srcFmt->toStraight) {
                        ImageBuffer tempBuf(width, 1, PixelFormatIDs::RGBA8_Straight,
                                            InitPolicy::Uninitialized, allocator());
                        srcFmt->toStraight(tempBuf.view().pixelAt(0, 0), overlapSrc, width, nullptr);
                        PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
                            overlapDst, tempBuf.view().pixelAt(0, 0), width, nullptr);
                    }
                    srcPos += width;
                    writePos = overlapEnd;
                }
            }
        }

        // 残りの非重複部分（全ての描画済み範囲より右） → converter
        if (writePos < curEndX && converter) {
            int width = curEndX - writePos;
            converter(canvasRow + static_cast<size_t>(writePos) * bytesPerPixel,
                      srcBytes + static_cast<size_t>(srcPos) * srcBpp, width);
        }

        // 描画済み範囲配列を更新（今回の範囲をマージ）
        // 重複する範囲を統合し、ソート順を維持
        if (drawnRanges_) {
            // 新しい範囲とマージ対象を特定
            int_fast16_t mergeStart = -1;  // マージ開始インデックス
            int_fast16_t mergeEnd = -1;    // マージ終了インデックス（含まない）
            int16_t newStart = curStartX;
            int16_t newEnd = curEndX;

            for (int_fast16_t j = 0; j < drawnCount; j++) {
                DataRange& drawn = drawnRanges_[j];
                // 重複または隣接している場合はマージ対象
                if (drawn.startX <= newEnd && drawn.endX >= newStart) {
                    if (mergeStart < 0) mergeStart = j;
                    mergeEnd = j + 1;
                    if (drawn.startX < newStart) newStart = drawn.startX;
                    if (drawn.endX > newEnd) newEnd = drawn.endX;
                }
            }

            if (mergeStart < 0) {
                // マージ対象なし: 新しい範囲を挿入（ソート位置を探す）
                int_fast16_t insertPos = 0;
                while (insertPos < drawnCount && drawnRanges_[insertPos].endX < curStartX) {
                    insertPos++;
                }
                // 後ろの要素をシフト
                for (int_fast16_t j = drawnCount; j > insertPos; j--) {
                    drawnRanges_[j] = drawnRanges_[j - 1];
                }
                drawnRanges_[insertPos] = DataRange{curStartX, curEndX};
                drawnCount++;
            } else {
                // マージ対象あり: 統合した範囲で置き換え
                drawnRanges_[mergeStart] = DataRange{newStart, newEnd};
                // マージで消費された範囲を詰める
                int_fast16_t removeCount = mergeEnd - mergeStart - 1;
                if (removeCount > 0) {
                    for (int_fast16_t j = mergeStart + 1; j < drawnCount - removeCount; j++) {
                        drawnRanges_[j] = drawnRanges_[j + removeCount];
                    }
                    drawnCount -= removeCount;
                }
            }
        } else {
            // drawnRanges_がnullの場合: 単一範囲として追跡（フォールバック）
            if (curStartX < fallbackStartX) fallbackStartX = curStartX;
            if (curEndX > fallbackEndX) fallbackEndX = curEndX;
            drawnCount = 1;  // 少なくとも1つの範囲がある
        }
    }

    if (drawnCount == 0) {
        return RenderResponse(ImageBuffer(), request.origin);
    }

    int16_t finalStartX, finalEndX;

    if (drawnRanges_) {
        // 描画済み範囲の間のギャップをゼロクリア
        // （左端・右端の未描画領域はcropで除去するためクリア不要）
        for (int_fast16_t i = 0; i < drawnCount - 1; i++) {
            int gapStart = drawnRanges_[i].endX;
            int gapEnd = drawnRanges_[i + 1].startX;
            if (gapStart < gapEnd) {
                std::memset(canvasRow + static_cast<size_t>(gapStart) * bytesPerPixel, 0,
                            static_cast<size_t>(gapEnd - gapStart) * bytesPerPixel);
            }
        }
        // 描画済み範囲の最小・最大でcrop
        finalStartX = drawnRanges_[0].startX;
        finalEndX = drawnRanges_[drawnCount - 1].endX;
    } else {
        // フォールバック: 単一範囲として扱う（ギャップなし）
        finalStartX = fallbackStartX;
        finalEndX = fallbackEndX;
    }

    // 左端・右端の未描画領域を除去
    canvasBuf.cropView(finalStartX, 0, finalEndX - finalStartX, 1);

    // originも調整（finalStartX分だけ右にシフト）
    int_fixed finalOriginX = canvasOriginX + to_fixed(finalStartX);

    return RenderResponse(std::move(canvasBuf), Point{finalOriginX, canvasOriginY});
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_COMPOSITE_NODE_H
