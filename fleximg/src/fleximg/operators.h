#ifndef FLEXIMG_OPERATORS_H
#define FLEXIMG_OPERATORS_H

#include "common.h"
#include "viewport.h"
#include "image_buffer.h"
#include "eval_result.h"
#include "image_types.h"
#include "node_graph.h"  // RenderRequest
#include <vector>
#include <memory>
#include <stdexcept>
#include <string>

namespace FLEXIMG_NAMESPACE {

// 前方宣言
struct GraphNode;

// ========================================================================
// OperatorInput - オペレーターへの入力
// ViewPort（データビュー）と座標情報のペア
// ========================================================================

struct OperatorInput {
    ViewPort view;
    float originX = 0;  // 基準点から見た画像左上の相対座標
    float originY = 0;

    OperatorInput() = default;
    OperatorInput(ViewPort v, float ox, float oy)
        : view(v), originX(ox), originY(oy) {}

    // EvalResultから構築（ビューを取得）
    OperatorInput(const EvalResult& result)
        : view(result.buffer.view()), originX(result.origin.x), originY(result.origin.y) {}

    bool isValid() const { return view.isValid(); }
};

// ========================================================================
// NodeOperator 基底クラス
// すべてのノード処理の共通インターフェース
// ========================================================================

class NodeOperator {
public:
    virtual ~NodeOperator() = default;

    // メイン処理: 入力群からEvalResultを生成
    // request: 下流からの処理要求（必要なサイズと基準座標）
    virtual EvalResult apply(const std::vector<OperatorInput>& inputs,
                            const RenderRequest& request) const = 0;

    // 入力数の制約
    virtual int getMinInputCount() const = 0;
    virtual int getMaxInputCount() const = 0;  // -1 = 無制限

    // 入出力フォーマット情報
    virtual PixelFormatID getPreferredInputFormat() const {
        return PixelFormatIDs::RGBA8_Straight;
    }
    virtual PixelFormatID getOutputFormat() const {
        return PixelFormatIDs::RGBA8_Straight;
    }

    // オペレーター名（デバッグ・ログ用）
    virtual const char* getName() const = 0;

    // 入力要求を計算: 出力要求に対して必要な入力範囲を返す
    // デフォルト: そのまま返す（入力拡大不要）
    // ブラーフィルタ等はオーバーライドしてカーネル半径分拡大
    virtual RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const {
        return outputRequest;
    }

protected:
    NodeOperator() = default;
};

// ========================================================================
// SingleInputOperator 基底クラス
// 1入力→1出力のオペレーター用便利クラス
// ========================================================================

class SingleInputOperator : public NodeOperator {
public:
    // NodeOperator::apply を実装
    EvalResult apply(const std::vector<OperatorInput>& inputs,
                    const RenderRequest& request) const override final {
        if (inputs.empty()) {
            throw std::invalid_argument("SingleInputOperator requires at least 1 input");
        }
        return applyToSingle(inputs[0], request);
    }

    // 派生クラスはこちらを実装
    virtual EvalResult applyToSingle(const OperatorInput& input,
                                     const RenderRequest& request) const = 0;

    int getMinInputCount() const override { return 1; }
    int getMaxInputCount() const override { return 1; }

protected:
    SingleInputOperator() = default;
};

// ========================================================================
// フィルタオペレーター群
// フィルタ処理を直接実装
// ========================================================================

// 明るさ調整オペレーター
class BrightnessOperator : public SingleInputOperator {
public:
    explicit BrightnessOperator(float brightness) : brightness_(brightness) {}
    EvalResult applyToSingle(const OperatorInput& input,
                            const RenderRequest& request) const override;
    const char* getName() const override { return "Brightness"; }

private:
    float brightness_;  // -1.0 ~ 1.0
};

// グレースケールオペレーター
class GrayscaleOperator : public SingleInputOperator {
public:
    GrayscaleOperator() = default;
    EvalResult applyToSingle(const OperatorInput& input,
                            const RenderRequest& request) const override;
    const char* getName() const override { return "Grayscale"; }
};

// ボックスブラーオペレーター
class BoxBlurOperator : public SingleInputOperator {
public:
    explicit BoxBlurOperator(int radius) : radius_(radius > 0 ? radius : 1) {}
    EvalResult applyToSingle(const OperatorInput& input,
                            const RenderRequest& request) const override;
    const char* getName() const override { return "BoxBlur"; }

    // カーネル半径分だけ入力要求を拡大
    RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const override {
        return outputRequest.expand(radius_);
    }

private:
    int radius_;  // ブラー半径（1以上）
};

// アルファ調整オペレーター
class AlphaOperator : public SingleInputOperator {
public:
    explicit AlphaOperator(float alpha) : alpha_(alpha) {}
    EvalResult applyToSingle(const OperatorInput& input,
                            const RenderRequest& request) const override;
    const char* getName() const override { return "Alpha"; }

    // AlphaOperator はPremultiplied形式でも処理可能
    PixelFormatID getPreferredInputFormat() const override {
        return PixelFormatIDs::RGBA16_Premultiplied;
    }
    PixelFormatID getOutputFormat() const override {
        return PixelFormatIDs::RGBA16_Premultiplied;
    }

private:
    float alpha_;  // アルファ乗数 (0.0 ~ 1.0)
};

// ========================================================================
// アフィン変換オペレーター
// 入力画像にアフィン変換を適用
// ========================================================================

class AffineOperator : public SingleInputOperator {
public:
    // matrix: 変換行列
    // outputOffsetX/Y: 出力バッファ内での基準点オフセット（inputSrcOriginからの差分）
    // outputWidth/Height: 出力サイズ（0以下の場合はrequest.width/heightを使用）
    AffineOperator(const AffineMatrix& matrix,
                   float outputOffsetX = 0, float outputOffsetY = 0,
                   int outputWidth = 0, int outputHeight = 0);

    EvalResult applyToSingle(const OperatorInput& input,
                            const RenderRequest& request) const override;
    const char* getName() const override { return "Affine"; }

    // アフィン変換はPremultiplied形式を要求
    PixelFormatID getPreferredInputFormat() const override {
        return PixelFormatIDs::RGBA16_Premultiplied;
    }
    PixelFormatID getOutputFormat() const override {
        return PixelFormatIDs::RGBA16_Premultiplied;
    }

private:
    AffineMatrix matrix_;
    float outputOffsetX_, outputOffsetY_;
    int outputWidth_, outputHeight_;
};

// ========================================================================
// 合成オペレーター
// 複数入力画像を重ね合わせる
// ========================================================================

class CompositeOperator : public NodeOperator {
public:
    CompositeOperator() = default;

    EvalResult apply(const std::vector<OperatorInput>& inputs,
                    const RenderRequest& request) const override;

    const char* getName() const override { return "Composite"; }
    int getMinInputCount() const override { return 1; }
    int getMaxInputCount() const override { return -1; }  // 無制限

    // 合成はPremultiplied形式を要求
    PixelFormatID getPreferredInputFormat() const override {
        return PixelFormatIDs::RGBA16_Premultiplied;
    }
    PixelFormatID getOutputFormat() const override {
        return PixelFormatIDs::RGBA16_Premultiplied;
    }

    // === 逐次合成用インターフェース ===

    // 透明キャンバスを作成（ImageBuffer + origin）
    static EvalResult createCanvas(const RenderRequest& request);

    // キャンバスに入力を合成（最初の入力用、memcpy最適化）
    // canvasOrigin: キャンバスの origin 座標
    // inputOrigin: 入力の origin 座標
    static void blendFirst(ViewPort& canvas, float canvasOriginX, float canvasOriginY,
                          const ViewPort& input, float inputOriginX, float inputOriginY);

    // キャンバスに入力を合成（2枚目以降、ブレンド処理）
    static void blendOnto(ViewPort& canvas, float canvasOriginX, float canvasOriginY,
                         const ViewPort& input, float inputOriginX, float inputOriginY);

    // 入力がリクエスト範囲を完全にカバーしているか判定
    static bool coversFullRequest(const OperatorInput& input, const RenderRequest& request);
};

// ========================================================================
// OperatorFactory
// ノード情報からオペレーターを生成するファクトリー
// ========================================================================

class OperatorFactory {
public:
    // フィルタノードからオペレーターを生成
    // filterType: "brightness", "grayscale", "blur", "alpha"
    // params: フィルタパラメータ配列
    static std::unique_ptr<NodeOperator> createFilterOperator(
        const std::string& filterType,
        const std::vector<float>& params
    );

    // アフィン変換オペレーターを生成
    // outputOffsetX/Y: 出力バッファ内での基準点オフセット
    static std::unique_ptr<NodeOperator> createAffineOperator(
        const AffineMatrix& matrix,
        float outputOffsetX, float outputOffsetY,
        int outputWidth, int outputHeight);

    // 合成オペレーターを生成
    static std::unique_ptr<NodeOperator> createCompositeOperator();
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_OPERATORS_H
