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
                inputs_[static_cast<size_t>(i)] = Port(this, static_cast<int>(i));
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

    // getDataRange/onPullProcess 間のキャッシュ
    struct DataRangeCache {
        Point origin = {INT32_MIN, INT32_MIN};  // キャッシュキー（無効値で初期化）
        int16_t startX = 0;
        int16_t endX = 0;
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
    // 有効な上流キャッシュを確保（最大で入力数分）
    {
        auto cacheInputCount = inputCount();
        if (cacheInputCount > 0 && allocator()) {
            size_t cacheSize = static_cast<size_t>(cacheInputCount) * sizeof(UpstreamCacheEntry);
            void* mem = allocator()->allocate(cacheSize, alignof(UpstreamCacheEntry));
            if (mem) {
                upstreamCache_ = static_cast<UpstreamCacheEntry*>(mem);
                upstreamCacheCapacity_ = cacheInputCount;
                validUpstreamCount_ = 0;  // calcUpstreamRangeUnionで設定される
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

    // キャッシュに保存
    rangeCache_.origin = request.origin;
    rangeCache_.startX = range.startX;
    rangeCache_.endX = range.endX;

    return range.hasData() ? range : DataRange{0, 0};
}

// onPullProcess: 複数の上流から画像を取得してunder合成
// under合成: 手前から奥へ処理し、不透明な部分は後のレイヤーをスキップ
// 最適化: height=1 前提（パイプラインは常にスキャンライン単位で処理）
RenderResponse CompositeNode::onPullProcess(const RenderRequest& request) {
    auto numInputs = inputCount();
    if (numInputs == 0) return RenderResponse();

    // キャンバス範囲を取得（キャッシュがあれば再利用）
    int16_t canvasStartX, canvasEndX;
    if (rangeCache_.origin.x == request.origin.x &&
        rangeCache_.origin.y == request.origin.y) {
        // キャッシュヒット: getDataRange()の計算結果を再利用
        canvasStartX = rangeCache_.startX;
        canvasEndX = rangeCache_.endX;
    } else {
        // キャッシュミス: 和集合を計算
        DataRange range = calcUpstreamRangeUnion(request);
        canvasStartX = range.startX;
        canvasEndX = range.endX;
    }

    // 有効なデータがない場合は空を返す（originは維持）
    if (canvasStartX >= canvasEndX) {
        return RenderResponse(ImageBuffer(), request.origin);
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

    // 有効範囲を追跡（バッファ座標系、0〜canvasWidth）
    int validStartX = canvasWidth;  // 左端（右端で初期化）
    int validEndX = 0;              // 右端（左端で初期化）

    // under合成: 有効な上流のみ順に評価して合成
    // 入力ポート0が最前面、以降が背面（キャッシュは同じ順序で格納）
    bool isFirstContent = true;
    for (int_fast16_t i = 0; i < validUpstreamCount_; i++) {
        // キャッシュから取得（hasData()チェック不要、キャッシュに入っている時点で有効）
        Node* upstream = upstreamCache_[i].node;
        // DataRange range = upstreamCache_[i].range;  // 現在未使用だが将来の最適化用

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

        const void* srcRow = inputResult.view().pixelAt(srcStartX, 0);
        uint8_t* dstRow = canvasRow + static_cast<size_t>(dstStartX) * bytesPerPixel;
        PixelFormatID srcFmt = inputResult.view().formatID;

        // 入力ごとに変換パスを解決（分岐なしの変換関数を取得）
        auto converter = resolveConverter(
            srcFmt, canvasFormat,
            &inputResult.buffer.auxInfo(), allocator());

        // 今回の描画範囲
        int curEndX = dstStartX + copyWidth;

        if (isFirstContent) {
            // 初回: 変換コピーのみ（余白ゼロクリア不要、cropViewで切り捨て）
            if (converter) {
                converter(dstRow, srcRow, copyWidth);
            }
            validStartX = dstStartX;
            validEndX = curEndX;
            isFirstContent = false;
        } else {
            // 2回目以降: 非重複領域はconvert、重複領域のみblend
            const auto* srcBytes = static_cast<const uint8_t*>(srcRow);
            size_t srcBpp = static_cast<size_t>(getBytesPerPixel(srcFmt));

            int overlapStart = std::max(dstStartX, validStartX);
            int overlapEnd   = std::min(curEndX, validEndX);

            if (overlapStart >= overlapEnd) {
                // 完全非重複: ギャップ領域をゼロクリアしてconvert
                if (curEndX <= validStartX) {
                    // 新規が左側: ギャップ [curEndX, validStartX)
                    std::memset(canvasRow + static_cast<size_t>(curEndX) * bytesPerPixel, 0,
                                static_cast<size_t>(validStartX - curEndX) * bytesPerPixel);
                } else if (dstStartX >= validEndX) {
                    // 新規が右側: ギャップ [validEndX, dstStartX)
                    std::memset(canvasRow + static_cast<size_t>(validEndX) * bytesPerPixel, 0,
                                static_cast<size_t>(dstStartX - validEndX) * bytesPerPixel);
                }
                if (converter) {
                    converter(dstRow, srcRow, copyWidth);
                }
            } else {
                // 重複あり: 3分割処理
                // 左側非重複 [dstStartX, overlapStart)
                int leftWidth = overlapStart - dstStartX;
                if (leftWidth > 0 && converter) {
                    converter(canvasRow + static_cast<size_t>(dstStartX) * bytesPerPixel,
                              srcBytes, leftWidth);
                }

                // 重複領域 [overlapStart, overlapEnd)
                int overlapWidth = overlapEnd - overlapStart;
                int overlapSrcOffset = overlapStart - dstStartX;
                const uint8_t* overlapSrc = srcBytes + static_cast<size_t>(overlapSrcOffset) * srcBpp;
                uint8_t* overlapDst = canvasRow + static_cast<size_t>(overlapStart) * bytesPerPixel;
                if (srcFmt->blendUnderStraight) {
                    srcFmt->blendUnderStraight(overlapDst, overlapSrc, overlapWidth, nullptr);
                } else if (srcFmt->toStraight) {
                    ImageBuffer tempBuf(overlapWidth, 1, PixelFormatIDs::RGBA8_Straight,
                                        InitPolicy::Uninitialized, allocator());
                    srcFmt->toStraight(tempBuf.view().pixelAt(0, 0), overlapSrc, overlapWidth, nullptr);
                    PixelFormatIDs::RGBA8_Straight->blendUnderStraight(
                        overlapDst, tempBuf.view().pixelAt(0, 0), overlapWidth, nullptr);
                }

                // 右側非重複 [overlapEnd, curEndX)
                int rightWidth = curEndX - overlapEnd;
                if (rightWidth > 0 && converter) {
                    int rightSrcOffset = overlapEnd - dstStartX;
                    converter(canvasRow + static_cast<size_t>(overlapEnd) * bytesPerPixel,
                              srcBytes + static_cast<size_t>(rightSrcOffset) * srcBpp,
                              rightWidth);
                }
            }

            // 有効範囲を更新
            if (dstStartX < validStartX) validStartX = dstStartX;
            if (curEndX > validEndX) validEndX = curEndX;
        }
    }

    if (validStartX >= validEndX) {
        return RenderResponse(ImageBuffer(), request.origin);
    }

    return RenderResponse(std::move(canvasBuf), Point{canvasOriginX, canvasOriginY});
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_COMPOSITE_NODE_H
