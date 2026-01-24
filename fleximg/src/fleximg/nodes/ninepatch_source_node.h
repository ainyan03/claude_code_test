#ifndef FLEXIMG_NINEPATCH_SOURCE_NODE_H
#define FLEXIMG_NINEPATCH_SOURCE_NODE_H

#include "../core/node.h"
#include "../core/perf_metrics.h"
#include "../image/viewport.h"
#include "../image/image_buffer.h"
#include "../operations/canvas_utils.h"
#include "source_node.h"

namespace FLEXIMG_NAMESPACE {

// ========================================================================
// NinePatchSourceNode - 9patch画像ソースノード
// ========================================================================
//
// Android の 9patch 画像に相当する、伸縮可能な画像ソースノード。
// 画像を9つの区画に分割し、角は固定サイズ、辺と中央は伸縮する。
//
// 9分割された領域:
// ┌────────┬─────────────────┬────────┐
// │ [0]    │      [1]        │   [2]  │  固定高さ
// │ 固定   │    横伸縮       │  固定  │
// ├────────┼─────────────────┼────────┤
// │ [3]    │      [4]        │   [5]  │  可変高さ
// │ 縦伸縮 │   両方向伸縮    │ 縦伸縮 │
// ├────────┼─────────────────┼────────┤
// │ [6]    │      [7]        │   [8]  │  固定高さ
// │ 固定   │    横伸縮       │  固定  │
// └────────┴─────────────────┴────────┘
//   固定幅    可変幅           固定幅
//
// - 入力ポート: 0（終端ノード）
// - 出力ポート: 1
//

class NinePatchSourceNode : public Node {
public:
    // コンストラクタ
    NinePatchSourceNode() {
        initPorts(0, 1);  // 入力0、出力1（終端ノード）
    }

    // ========================================
    // 初期化メソッド
    // ========================================

    // 通常画像 + 境界座標を明示指定（上級者向け/内部用）
    // left/top/right/bottom: 各角の固定サイズ（ピクセル）
    void setupWithBounds(const ViewPort& image,
                         int16_t left, int16_t top,
                         int16_t right, int16_t bottom) {
        source_ = image;
        srcLeft_ = left;
        srcTop_ = top;
        srcRight_ = right;
        srcBottom_ = bottom;
        // クリッピングなしの初期状態
        effectiveSrcLeft_ = left;
        effectiveSrcRight_ = right;
        effectiveSrcTop_ = top;
        effectiveSrcBottom_ = bottom;
        sourceValid_ = image.isValid();
        geometryValid_ = false;

        // 各区画のソースサイズを計算
        calcSrcPatchSizes();
    }

    // 9patch互換画像（外周1pxがメタデータ）を渡す（メインAPI）
    // 外周1pxを解析して境界座標を自動取得、内部画像を抽出
    void setupFromNinePatch(const ViewPort& ninePatchImage) {
        if (!ninePatchImage.isValid() || ninePatchImage.width < 3 || ninePatchImage.height < 3) {
            sourceValid_ = false;
            return;
        }

        // 黒ピクセル判定ラムダ（RGBA8_Straight: R=0, G=0, B=0, A>0）
        auto isBlack = [&](int x, int y) -> bool {
            const uint8_t* pixel = static_cast<const uint8_t*>(ninePatchImage.pixelAt(x, y));
            if (!pixel) return false;
            return pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] > 0;
        };

        // 内部画像（外周1pxを除く）を抽出
        ViewPort innerImage = view_ops::subView(ninePatchImage,
            1, 1, ninePatchImage.width - 2, ninePatchImage.height - 2);

        // 上辺（y=0）のメタデータを解析 → 横方向の伸縮領域
        int16_t stretchXStart = -1, stretchXEnd = -1;
        for (int16_t x = 1; x < ninePatchImage.width - 1; x++) {
            if (isBlack(x, 0)) {
                if (stretchXStart < 0) stretchXStart = x - 1;  // 外周を除いた座標
                stretchXEnd = x - 1;
            }
        }

        // 左辺（x=0）のメタデータを解析 → 縦方向の伸縮領域
        int16_t stretchYStart = -1, stretchYEnd = -1;
        for (int16_t y = 1; y < ninePatchImage.height - 1; y++) {
            if (isBlack(0, y)) {
                if (stretchYStart < 0) stretchYStart = y - 1;  // 外周を除いた座標
                stretchYEnd = y - 1;
            }
        }

        // 境界座標を計算
        int16_t left = (stretchXStart >= 0) ? stretchXStart : 0;
        int16_t right = (stretchXEnd >= 0) ? (innerImage.width - 1 - stretchXEnd) : 0;
        int16_t top = (stretchYStart >= 0) ? stretchYStart : 0;
        int16_t bottom = (stretchYEnd >= 0) ? (innerImage.height - 1 - stretchYEnd) : 0;

        // setupWithBounds を呼び出し
        setupWithBounds(innerImage, left, top, right, bottom);
    }

    // 出力サイズ設定（小数対応）
    void setOutputSize(float width, float height) {
        if (outputWidth_ != width || outputHeight_ != height) {
            outputWidth_ = width;
            outputHeight_ = height;
            geometryValid_ = false;
        }
    }

    // 基準点設定（デフォルトは左上 (0,0)）
    void setOrigin(int_fixed x, int_fixed y) {
        if (originX_ != x || originY_ != y) {
            originX_ = x;
            originY_ = y;
            geometryValid_ = false;  // アフィン行列の再計算が必要
        }
    }

    // 配置位置設定（アフィン行列のtx/tyに加算）
    void setPosition(float x, float y) {
        if (positionX_ != x || positionY_ != y) {
            positionX_ = x;
            positionY_ = y;
            geometryValid_ = false;  // アフィン行列の再計算が必要
        }
    }

    // 補間モード設定（内部の全SourceNodeに適用）
    void setInterpolationMode(InterpolationMode mode) {
        if (interpolationMode_ != mode) {
            interpolationMode_ = mode;
            geometryValid_ = false;  // ソースビュー再設定が必要
        }
        for (int i = 0; i < 9; i++) {
            patches_[i].setInterpolationMode(mode);
        }
    }

    // ========================================
    // アクセサ
    // ========================================

    float outputWidth() const { return outputWidth_; }
    float outputHeight() const { return outputHeight_; }
    int_fixed originX() const { return originX_; }
    int_fixed originY() const { return originY_; }

    // 境界座標（読み取り用）
    int16_t srcLeft() const { return srcLeft_; }
    int16_t srcTop() const { return srcTop_; }
    int16_t srcRight() const { return srcRight_; }
    int16_t srcBottom() const { return srcBottom_; }

    const char* name() const override { return "NinePatchSourceNode"; }
    int nodeTypeForMetrics() const override { return NodeType::NinePatch; }

    // ========================================
    // Template Method フック
    // ========================================

    // onPullPrepare: 各区画のSourceNodeにPrepareRequestを伝播
    PrepareResponse onPullPrepare(const PrepareRequest& request) override;

    // onPullFinalize: 各区画のSourceNodeに終了を伝播
    void onPullFinalize() override;

    // onPullProcess: 全9区画を処理して合成
    RenderResponse onPullProcess(const RenderRequest& request) override;

private:
    // 内部SourceNode（9区画）
    SourceNode patches_[9];

    // 元画像
    ViewPort source_;
    bool sourceValid_ = false;

    // 区画境界（ソース座標）
    int16_t srcLeft_ = 0;    // 左端からの固定幅
    int16_t srcTop_ = 0;     // 上端からの固定高さ
    int16_t srcRight_ = 0;   // 右端からの固定幅
    int16_t srcBottom_ = 0;  // 下端からの固定高さ

    // クリッピング適用後の固定部サイズ（出力サイズが固定部合計より小さい場合に使用）
    int16_t effectiveSrcLeft_ = 0;
    int16_t effectiveSrcRight_ = 0;
    int16_t effectiveSrcTop_ = 0;
    int16_t effectiveSrcBottom_ = 0;

    // 出力サイズ（小数対応）
    float outputWidth_ = 0.0f;
    float outputHeight_ = 0.0f;

    // 基準点（出力座標系）
    int_fixed originX_ = 0;
    int_fixed originY_ = 0;

    // 配置位置（アフィン行列のtx/tyに加算）
    float positionX_ = 0.0f;
    float positionY_ = 0.0f;

    // 補間モード
    InterpolationMode interpolationMode_ = InterpolationMode::Nearest;

    // ジオメトリ計算結果（小数対応）
    bool geometryValid_ = false;
    float patchWidths_[3] = {0, 0, 0};   // [左固定, 中央伸縮, 右固定]
    float patchHeights_[3] = {0, 0, 0};  // [上固定, 中央伸縮, 下固定]
    float patchOffsetX_[3] = {0, 0, 0};  // 各列の出力X開始位置
    float patchOffsetY_[3] = {0, 0, 0};  // 各行の出力Y開始位置

    // ソース画像内の各区画のサイズ
    int16_t srcPatchW_[3] = {0, 0, 0};     // 各列のソース幅
    int16_t srcPatchH_[3] = {0, 0, 0};     // 各行のソース高さ

    // 各区画のスケール行列（伸縮用）
    AffineMatrix patchScales_[9];
    bool patchNeedsAffine_[9] = {false};   // スケールが1.0でない場合true

    // ========================================
    // 内部メソッド
    // ========================================

    int getPatchIndex(int col, int row) const {
        return row * 3 + col;
    }

    // 各区画のソースサイズを計算（初期化時に呼び出し）
    void calcSrcPatchSizes();

    // 1軸方向のクリッピング計算（横/縦共通）
    void calcAxisClipping(float outputSize, int16_t srcFixed0, int16_t srcFixed2,
                          float& outWidth0, float& outWidth1, float& outWidth2,
                          int16_t& effSrc0, int16_t& effSrc2);

    // 出力サイズ変更時にジオメトリを再計算
    void updatePatchGeometry();
};

} // namespace FLEXIMG_NAMESPACE

// =============================================================================
// 実装部
// =============================================================================
#ifdef FLEXIMG_IMPLEMENTATION

namespace FLEXIMG_NAMESPACE {

// ============================================================================
// NinePatchSourceNode - Template Method フック実装
// ============================================================================

PrepareResponse NinePatchSourceNode::onPullPrepare(const PrepareRequest& request) {
    // ジオメトリ計算（まだなら）
    if (!geometryValid_) {
        updatePatchGeometry();
    }

    // 各区画のSourceNodeにPrepareRequestを伝播（スケール行列付き）
    for (int i = 0; i < 9; i++) {
        PrepareRequest patchRequest = request;

        // 親のアフィン行列と区画のスケール行列を合成
        if (patchNeedsAffine_[i]) {
            if (request.hasAffine) {
                // 親アフィン × 区画スケール
                patchRequest.affineMatrix = request.affineMatrix * patchScales_[i];
            } else {
                patchRequest.affineMatrix = patchScales_[i];
            }
            patchRequest.hasAffine = true;
        }
        // patchNeedsAffine_[i] == false の場合、親のアフィンをそのまま使用

        patches_[i].pullPrepare(patchRequest);
    }

    // NinePatchSourceNodeは終端なので上流への伝播なし
    // プルアフィン変換がある場合、出力側で必要なAABBを計算
    PrepareResponse result;
    result.status = PrepareStatus::Prepared;
    result.preferredFormat = source_.formatID;

    if (request.hasAffine) {
        // positionを含めた行列を計算
        AffineMatrix combinedMatrix = request.affineMatrix;
        float transformedPosX = combinedMatrix.a * positionX_ + combinedMatrix.b * positionY_;
        float transformedPosY = combinedMatrix.c * positionX_ + combinedMatrix.d * positionY_;
        combinedMatrix.tx += transformedPosX;
        combinedMatrix.ty += transformedPosY;

        // 出力矩形に順変換を適用して出力側のAABBを計算
        calcAffineAABB(
            static_cast<int>(outputWidth_), static_cast<int>(outputHeight_),
            {originX_, originY_},
            combinedMatrix,
            result.width, result.height, result.origin);
    } else {
        // アフィンなしの場合はそのまま（positionを含める）
        result.width = static_cast<int16_t>(outputWidth_);
        result.height = static_cast<int16_t>(outputHeight_);
        // originをposition分シフト
        result.origin.x = originX_ - float_to_fixed(positionX_);
        result.origin.y = originY_ - float_to_fixed(positionY_);
    }
    return result;
}

void NinePatchSourceNode::onPullFinalize() {
    for (int i = 0; i < 9; i++) {
        patches_[i].pullFinalize();
    }
    finalize();
}

RenderResponse NinePatchSourceNode::onPullProcess(const RenderRequest& request) {
    if (!sourceValid_ || outputWidth_ <= 0 || outputHeight_ <= 0) {
        return RenderResponse();
    }

    // ジオメトリ計算（まだなら）
    if (!geometryValid_) {
        updatePatchGeometry();
    }

    // 描画順序: 伸縮パッチ → 固定パッチ
    // 斜めアフィン時にパッチ継ぎ目のエッジが綺麗に処理される
    constexpr int drawOrder[9] = {
        4,           // 中央パッチ（両方向伸縮）を最初に
        1, 3, 5, 7,  // 伸縮パッチ（辺）
        0, 2, 6, 8   // 固定パッチ（角）を最後に
    };

    // 各パッチのデータ範囲を収集し、和集合を計算
    int16_t canvasStartX = request.width;
    int16_t canvasEndX = 0;
    for (int i : drawOrder) {
        int col = i % 3;
        int row = i / 3;
        if (patchWidths_[col] <= 0 || patchHeights_[row] <= 0) {
            continue;
        }
        DataRange range = patches_[i].getDataRange(request);
        if (range.hasData()) {
            if (range.startX < canvasStartX) canvasStartX = range.startX;
            if (range.endX > canvasEndX) canvasEndX = range.endX;
        }
    }

    // 有効なデータがない場合は空を返す
    if (canvasStartX >= canvasEndX) {
        return RenderResponse();
    }

    int16_t canvasWidth = canvasEndX - canvasStartX;

    // キャンバス作成（透明で初期化、必要幅のみ確保）
    // canvasStartX分だけoriginをシフト
    int_fixed canvasOriginX = request.origin.x - to_fixed(canvasStartX);
    int_fixed canvasOriginY = request.origin.y;

    ImageBuffer canvasBuf = canvas_utils::createCanvas(canvasWidth, request.height, InitPolicy::Zero, allocator());
    ViewPort canvasView = canvasBuf.view();

    // 全9区画を処理
    for (int i : drawOrder) {
        // サイズ0の区画はスキップ
        int col = i % 3;
        int row = i / 3;
        if (patchWidths_[col] <= 0 || patchHeights_[row] <= 0) {
            continue;
        }

        // 範囲外のパッチはスキップ
        DataRange range = patches_[i].getDataRange(request);
        if (!range.hasData()) continue;

        RenderResponse patchResult = patches_[i].pullProcess(request);
        if (!patchResult.isValid()) continue;

        // フォーマット変換
        patchResult = canvas_utils::ensureBlendableFormat(std::move(patchResult));

        // キャンバスに配置（全パッチ上書き）
        // NinePatchではパッチ同士の重なりは単純上書きで良い
        canvas_utils::placeFirst(canvasView, canvasOriginX, canvasOriginY,
                                 patchResult.view(), patchResult.origin.x, patchResult.origin.y);
    }

    return RenderResponse(std::move(canvasBuf), Point{canvasOriginX, canvasOriginY});
}

// ============================================================================
// NinePatchSourceNode - private ヘルパーメソッド実装
// ============================================================================

void NinePatchSourceNode::calcSrcPatchSizes() {
    srcPatchW_[0] = srcLeft_;
    srcPatchW_[1] = source_.width - srcLeft_ - srcRight_;
    srcPatchW_[2] = srcRight_;
    srcPatchH_[0] = srcTop_;
    srcPatchH_[1] = source_.height - srcTop_ - srcBottom_;
    srcPatchH_[2] = srcBottom_;
}

void NinePatchSourceNode::calcAxisClipping(float outputSize, int16_t srcFixed0, int16_t srcFixed2,
                                           float& outWidth0, float& outWidth1, float& outWidth2,
                                           int16_t& effSrc0, int16_t& effSrc2) {
    // クリッピング時もソースビューは元のサイズを維持し、スケールで縮小
    float totalFixed = static_cast<float>(srcFixed0 + srcFixed2);
    if (outputSize < totalFixed && totalFixed > 0) {
        float ratio = outputSize / totalFixed;
        outWidth0 = srcFixed0 * ratio;
        outWidth1 = 0.0f;
        outWidth2 = outputSize - outWidth0;
        // ソースビューは元のサイズを維持（スケールで縮小して滑らかな描画を実現）
        effSrc0 = srcFixed0;
        effSrc2 = srcFixed2;
    } else {
        effSrc0 = srcFixed0;
        effSrc2 = srcFixed2;
        outWidth0 = static_cast<float>(srcFixed0);
        outWidth1 = outputSize - srcFixed0 - srcFixed2;
        outWidth2 = static_cast<float>(srcFixed2);
    }
}

void NinePatchSourceNode::updatePatchGeometry() {
    if (!sourceValid_) return;

    // 横方向・縦方向のクリッピング計算
    calcAxisClipping(outputWidth_, srcLeft_, srcRight_,
                     patchWidths_[0], patchWidths_[1], patchWidths_[2],
                     effectiveSrcLeft_, effectiveSrcRight_);
    calcAxisClipping(outputHeight_, srcTop_, srcBottom_,
                     patchHeights_[0], patchHeights_[1], patchHeights_[2],
                     effectiveSrcTop_, effectiveSrcBottom_);

    // 各区画の出力開始位置
    patchOffsetX_[0] = 0.0f;
    patchOffsetX_[1] = patchWidths_[0];
    patchOffsetX_[2] = outputWidth_ - patchWidths_[2];
    patchOffsetY_[0] = 0.0f;
    patchOffsetY_[1] = patchHeights_[0];
    patchOffsetY_[2] = outputHeight_ - patchHeights_[2];

    // 各列/行の有効ソースサイズと開始位置
    int16_t effW[3] = { effectiveSrcLeft_, srcPatchW_[1], effectiveSrcRight_ };
    int16_t effH[3] = { effectiveSrcTop_, srcPatchH_[1], effectiveSrcBottom_ };
    int16_t srcX[3] = { 0, srcLeft_, static_cast<int16_t>(source_.width - effectiveSrcRight_) };
    int16_t srcY[3] = { 0, srcTop_, static_cast<int16_t>(source_.height - effectiveSrcBottom_) };

    // クリッピング状態を判定（出力サイズが固定部の合計より小さいか）
    bool hClipping = outputWidth_ < static_cast<float>(srcLeft_ + srcRight_);
    bool vClipping = outputHeight_ < static_cast<float>(srcTop_ + srcBottom_);

    bool hasHStretch = effW[1] > 0 && !hClipping;  // 横方向伸縮部が存在かつクリッピングなし
    bool hasVStretch = effH[1] > 0 && !vClipping;  // 縦方向伸縮部が存在かつクリッピングなし

    float originXf = static_cast<float>(originX_) / INT_FIXED_ONE;
    float originYf = static_cast<float>(originY_) / INT_FIXED_ONE;

    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            int idx = row * 3 + col;

            // オーバーラップ量（固定部→伸縮部方向に拡張）
            int16_t dx = 0, dy = 0, dw = 0, dh = 0;

            // 横方向オーバーラップ
            if (hasHStretch) {
                // 通常時: 固定部 → 伸縮部方向の拡張
                if (col == 0 && effW[0] > 0) { dw = 1; }           // 左固定: 右に拡張
                else if (col == 2 && effW[2] > 0) { dx = -1; dw = 1; }  // 右固定: 左に拡張
            } else if (hClipping) {
                // クリッピング時: 左固定と右固定が直接隣接
                if (col == 0 && effW[0] > 0 && effW[2] > 0) { dw = 1; }
                else if (col == 2 && effW[0] > 0 && effW[2] > 0) { dx = -1; dw = 1; }
            }

            // 縦方向オーバーラップ
            if (hasVStretch) {
                // 通常時: 固定部 → 伸縮部方向の拡張
                if (row == 0 && effH[0] > 0) { dh = 1; }           // 上固定: 下に拡張
                else if (row == 2 && effH[2] > 0) { dy = -1; dh = 1; }  // 下固定: 上に拡張
            } else if (vClipping) {
                // クリッピング時: 上固定と下固定が直接隣接
                if (row == 0 && effH[0] > 0 && effH[2] > 0) { dh = 1; }
                else if (row == 2 && effH[0] > 0 && effH[2] > 0) { dy = -1; dh = 1; }
            }

            // ソースビュー設定
            if (effW[col] > 0 && effH[row] > 0) {
                ViewPort subView = view_ops::subView(source_,
                    srcX[col] + dx, srcY[row] + dy, effW[col] + dw, effH[row] + dh);
                patches_[idx].setSource(subView);
                patches_[idx].setOrigin(0, 0);
            }

            // スケール計算
            float scaleX = 1.0f, scaleY = 1.0f;

            // 横方向スケール
            if (col == 1 && srcPatchW_[1] > 0) {
                // 伸縮部
                int16_t effSrcW = srcPatchW_[1];
                if (interpolationMode_ == InterpolationMode::Bilinear && effSrcW > 1) effSrcW -= 1;
                scaleX = patchWidths_[1] / effSrcW;
            } else if (hClipping && effW[col] > 0) {
                // クリッピング時の固定部（出力幅/ソース幅）
                scaleX = patchWidths_[col] / effW[col];
            }

            // 縦方向スケール
            if (row == 1 && srcPatchH_[1] > 0) {
                // 伸縮部
                int16_t effSrcH = srcPatchH_[1];
                if (interpolationMode_ == InterpolationMode::Bilinear && effSrcH > 1) effSrcH -= 1;
                scaleY = patchHeights_[1] / effSrcH;
            } else if (vClipping && effH[row] > 0) {
                // クリッピング時の固定部（出力高さ/ソース高さ）
                scaleY = patchHeights_[row] / effH[row];
            }

            // 平行移動量
            float tx = patchOffsetX_[col] + dx - originXf + positionX_;
            float ty = patchOffsetY_[row] + dy - originYf + positionY_;

            // バイリニア時の伸縮部位置補正
            if (interpolationMode_ == InterpolationMode::Bilinear) {
                if (col == 1 && srcPatchW_[1] > 1) tx -= scaleX * 0.5f;
                if (row == 1 && srcPatchH_[1] > 1) ty -= scaleY * 0.5f;
            }

            patchScales_[idx] = AffineMatrix(scaleX, 0.0f, 0.0f, scaleY, tx, ty);
            patchNeedsAffine_[idx] = true;
        }
    }

    geometryValid_ = true;
}

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_IMPLEMENTATION

#endif // FLEXIMG_NINEPATCH_SOURCE_NODE_H
