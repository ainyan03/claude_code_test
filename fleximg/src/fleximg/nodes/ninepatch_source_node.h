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
        sourceValid_ = image.isValid();
        geometryValid_ = false;

        // 各区画のSourceNodeを初期化
        setupPatchSourceNodes();
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

    // 出力サイズ設定
    void setOutputSize(int16_t width, int16_t height) {
        if (outputWidth_ != width || outputHeight_ != height) {
            outputWidth_ = width;
            outputHeight_ = height;
            geometryValid_ = false;
        }
    }

    // 基準点設定（デフォルトは左上 (0,0)）
    void setOrigin(int_fixed8 x, int_fixed8 y) {
        if (originX_ != x || originY_ != y) {
            originX_ = x;
            originY_ = y;
            geometryValid_ = false;  // アフィン行列の再計算が必要
        }
    }

    // ========================================
    // アクセサ
    // ========================================

    int16_t outputWidth() const { return outputWidth_; }
    int16_t outputHeight() const { return outputHeight_; }
    int_fixed8 originX() const { return originX_; }
    int_fixed8 originY() const { return originY_; }

    // 境界座標（読み取り用）
    int16_t srcLeft() const { return srcLeft_; }
    int16_t srcTop() const { return srcTop_; }
    int16_t srcRight() const { return srcRight_; }
    int16_t srcBottom() const { return srcBottom_; }

    const char* name() const override { return "NinePatchSourceNode"; }

    // ========================================
    // PrepareRequest対応
    // ========================================

    bool pullPrepare(const PrepareRequest& request) override {
        // 循環参照検出
        if (pullPrepareState_ == PrepareState::Preparing) {
            pullPrepareState_ = PrepareState::CycleError;
            return false;
        }
        if (pullPrepareState_ == PrepareState::Prepared) {
            return true;
        }
        if (pullPrepareState_ == PrepareState::CycleError) {
            return false;
        }

        pullPrepareState_ = PrepareState::Preparing;

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

        pullPrepareState_ = PrepareState::Prepared;
        return true;
    }

    void pullFinalize() override {
        if (pullPrepareState_ == PrepareState::Idle) {
            return;
        }
        pullPrepareState_ = PrepareState::Idle;

        for (int i = 0; i < 9; i++) {
            patches_[i].pullFinalize();
        }
    }

    // ========================================
    // プル型インターフェース
    // ========================================

    RenderResult pullProcess(const RenderRequest& request) override {
        if (!sourceValid_ || outputWidth_ <= 0 || outputHeight_ <= 0) {
            return RenderResult();
        }

        // ジオメトリ計算（まだなら）
        if (!geometryValid_) {
            updatePatchGeometry();
        }

        // キャンバス作成
        ImageBuffer canvasBuf = canvas_utils::createCanvas(request.width, request.height);
        ViewPort canvasView = canvasBuf.view();

        // 全9区画を処理
        // 描画順序: 固定パッチ（角）→ 伸縮パッチ（辺、中央）
        // オーバーラップ部分は伸縮パッチで上書きされる
        constexpr int drawOrder[9] = {
            0, 2, 6, 8,  // 固定パッチ（角）を先に
            1, 3, 5, 7,  // 伸縮パッチ（辺）を後から
            4            // 中央パッチを最後に
        };

        bool first = true;
        for (int i : drawOrder) {
            // サイズ0の区画はスキップ
            int col = i % 3;
            int row = i / 3;
            if (patchWidths_[col] <= 0 || patchHeights_[row] <= 0) {
                continue;
            }

            RenderResult patchResult = patches_[i].pullProcess(request);
            if (!patchResult.isValid()) continue;

            // フォーマット変換
            patchResult = canvas_utils::ensureBlendableFormat(std::move(patchResult));

            // キャンバスに配置
            // 固定パッチ（col≠1 かつ row≠1）以外は上書き（placeFirst）
            bool isCornerPatch = (col != 1 && row != 1);
            if (first) {
                canvas_utils::placeFirst(canvasView, request.origin.x, request.origin.y,
                                         patchResult.view(), patchResult.origin.x, patchResult.origin.y);
                first = false;
            } else if (isCornerPatch) {
                // 固定パッチはブレンド（半透明対応）
                canvas_utils::placeOnto(canvasView, request.origin.x, request.origin.y,
                                        patchResult.view(), patchResult.origin.x, patchResult.origin.y);
            } else {
                // 伸縮パッチは上書き（オーバーラップ部分を完全に塗りつぶす）
                canvas_utils::placeFirst(canvasView, request.origin.x, request.origin.y,
                                         patchResult.view(), patchResult.origin.x, patchResult.origin.y);
            }
        }

        return RenderResult(std::move(canvasBuf), request.origin);
    }

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

    // 出力サイズ
    int16_t outputWidth_ = 0;
    int16_t outputHeight_ = 0;

    // 基準点（出力座標系）
    int_fixed8 originX_ = 0;
    int_fixed8 originY_ = 0;

    // ジオメトリ計算結果
    bool geometryValid_ = false;
    int16_t patchWidths_[3] = {0, 0, 0};   // [左固定, 中央伸縮, 右固定]
    int16_t patchHeights_[3] = {0, 0, 0};  // [上固定, 中央伸縮, 下固定]
    int16_t patchOffsetX_[3] = {0, 0, 0};  // 各列の出力X開始位置
    int16_t patchOffsetY_[3] = {0, 0, 0};  // 各行の出力Y開始位置

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

    // オーバーラップ量を計算（ドット抜け対策）
    // dx, dy: ソース/出力の開始位置オフセット（負値 = 左/上方向に拡張）
    // dw, dh: ソース/出力のサイズ増分
    void calculateOverlap(int col, int row, int16_t& dx, int16_t& dy, int16_t& dw, int16_t& dh) const {
        dx = dy = dw = dh = 0;

        bool hasHStretch = srcPatchW_[1] > 0;  // 横方向伸縮部が存在
        bool hasVStretch = srcPatchH_[1] > 0;  // 縦方向伸縮部が存在

        // 横方向の拡張（左列は右に、右列は左に）
        if (hasHStretch) {
            if (col == 0 && srcPatchW_[0] > 0) { dw = 1; }           // 左列: 右に拡張
            else if (col == 2 && srcPatchW_[2] > 0) { dx = -1; dw = 1; }  // 右列: 左に拡張
        }

        // 縦方向の拡張（上行は下に、下行は上に）
        if (hasVStretch) {
            if (row == 0 && srcPatchH_[0] > 0) { dh = 1; }           // 上行: 下に拡張
            else if (row == 2 && srcPatchH_[2] > 0) { dy = -1; dh = 1; }  // 下行: 上に拡張
        }
    }

    // 各区画のSourceNodeを初期化
    void setupPatchSourceNodes() {
        if (!sourceValid_) return;

        // ソース画像の中央領域サイズ
        int16_t srcCenterW = source_.width - srcLeft_ - srcRight_;
        int16_t srcCenterH = source_.height - srcTop_ - srcBottom_;

        // 各列/行のソースサイズを保存（スケール計算用 - オーバーラップ前の値）
        srcPatchW_[0] = srcLeft_;
        srcPatchW_[1] = srcCenterW;
        srcPatchW_[2] = srcRight_;

        srcPatchH_[0] = srcTop_;
        srcPatchH_[1] = srcCenterH;
        srcPatchH_[2] = srcBottom_;

        // 各列/行のソース開始位置
        int16_t srcPatchX[3] = { 0, srcLeft_, static_cast<int16_t>(srcLeft_ + srcCenterW) };
        int16_t srcPatchY[3] = { 0, srcTop_, static_cast<int16_t>(srcTop_ + srcCenterH) };

        // 各区画のSourceNodeにsubViewを設定（オーバーラップ適用）
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                int idx = getPatchIndex(col, row);
                int16_t w = srcPatchW_[col];
                int16_t h = srcPatchH_[row];

                if (w > 0 && h > 0) {
                    // オーバーラップ量を計算
                    int16_t dx, dy, dw, dh;
                    calculateOverlap(col, row, dx, dy, dw, dh);

                    // ソースビューを拡張（オーバーラップ適用）
                    int16_t sx = srcPatchX[col] + dx;
                    int16_t sy = srcPatchY[row] + dy;
                    int16_t sw = w + dw;
                    int16_t sh = h + dh;

                    ViewPort subView = view_ops::subView(source_, sx, sy, sw, sh);
                    patches_[idx].setSource(subView);
                    patches_[idx].setOrigin(0, 0);
                }
            }
        }
    }

    // 出力サイズ変更時にジオメトリを再計算
    void updatePatchGeometry() {
        // 出力サイズから各区画のサイズを計算
        // 角: 固定サイズ
        // 辺・中央: 伸縮サイズ

        patchWidths_[0] = srcLeft_;                              // 左列: 固定
        patchWidths_[2] = srcRight_;                             // 右列: 固定
        patchWidths_[1] = outputWidth_ - srcLeft_ - srcRight_;   // 中央列: 伸縮

        patchHeights_[0] = srcTop_;                              // 上段: 固定
        patchHeights_[2] = srcBottom_;                           // 下段: 固定
        patchHeights_[1] = outputHeight_ - srcTop_ - srcBottom_; // 中央段: 伸縮

        // 各区画の出力開始位置
        patchOffsetX_[0] = 0;
        patchOffsetX_[1] = srcLeft_;
        patchOffsetX_[2] = outputWidth_ - srcRight_;

        patchOffsetY_[0] = 0;
        patchOffsetY_[1] = srcTop_;
        patchOffsetY_[2] = outputHeight_ - srcBottom_;

        // 各区画のスケール行列とoriginを計算
        // 角（[0], [2], [6], [8]）: スケール = 1.0（固定サイズ）
        // 横伸縮（[1], [7]）: scaleX のみ
        // 縦伸縮（[3], [5]）: scaleY のみ
        // 両方向（[4]）: scaleX と scaleY
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                int idx = getPatchIndex(col, row);
                float scaleX = 1.0f;
                float scaleY = 1.0f;

                // 中央列（col=1）は横方向に伸縮
                if (col == 1 && srcPatchW_[1] > 0) {
                    scaleX = static_cast<float>(patchWidths_[1]) / srcPatchW_[1];
                }

                // 中央行（row=1）は縦方向に伸縮
                if (row == 1 && srcPatchH_[1] > 0) {
                    scaleY = static_cast<float>(patchHeights_[1]) / srcPatchH_[1];
                }

                // オーバーラップ量を取得
                int16_t dx, dy, dw, dh;
                calculateOverlap(col, row, dx, dy, dw, dh);

                // アフィン行列を設定（スケール + 平行移動）
                // 出力座標P → ソース座標S: S = (P - patchOffset) / scale
                // アフィン行列（S → P）: P = S * scale + patchOffset
                // 平行移動はorigin相対座標で指定
                // オーバーラップにより開始位置がずれる場合は dx, dy を加算
                float tx = static_cast<float>(patchOffsetX_[col] + dx) - from_fixed8(originX_);
                float ty = static_cast<float>(patchOffsetY_[row] + dy) - from_fixed8(originY_);
                patchScales_[idx] = AffineMatrix(scaleX, 0.0f, 0.0f, scaleY, tx, ty);
                patchNeedsAffine_[idx] = true;  // 平行移動があるので常にtrue

                // 各区画のoriginは(0, 0)（平行移動で位置調整済み）
                patches_[idx].setOrigin(0, 0);
            }
        }

        geometryValid_ = true;
    }
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_NINEPATCH_SOURCE_NODE_H
