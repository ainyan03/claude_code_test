#ifndef FLEXIMG_OPERATORS_H
#define FLEXIMG_OPERATORS_H

#include "common.h"
#include "viewport.h"
#include "image_types.h"
#include <vector>
#include <memory>
#include <stdexcept>
#include <string>

namespace FLEXIMG_NAMESPACE {

// 前方宣言
struct GraphNode;

// ========================================================================
// オペレーター実行コンテキスト
// オペレーター実行時に必要な共通情報を保持
// ========================================================================

struct OperatorContext {
    int canvasWidth;     // キャンバス幅
    int canvasHeight;    // キャンバス高さ
    double dstOriginX;   // 出力基準点X
    double dstOriginY;   // 出力基準点Y

    OperatorContext()
        : canvasWidth(0), canvasHeight(0), dstOriginX(0), dstOriginY(0) {}

    OperatorContext(int w, int h, double ox, double oy)
        : canvasWidth(w), canvasHeight(h), dstOriginX(ox), dstOriginY(oy) {}
};

// ========================================================================
// NodeOperator 基底クラス
// すべてのノード処理の共通インターフェース
// ========================================================================

class NodeOperator {
public:
    virtual ~NodeOperator() = default;

    // メイン処理: 入力群からViewPortを生成
    virtual ViewPort apply(const std::vector<ViewPort>& inputs,
                          const OperatorContext& ctx) const = 0;

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
    ViewPort apply(const std::vector<ViewPort>& inputs,
                  const OperatorContext& ctx) const override final {
        if (inputs.empty()) {
            throw std::invalid_argument("SingleInputOperator requires at least 1 input");
        }
        return applyToSingle(inputs[0], ctx);
    }

    // 派生クラスはこちらを実装
    virtual ViewPort applyToSingle(const ViewPort& input,
                                   const OperatorContext& ctx) const = 0;

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
    ViewPort applyToSingle(const ViewPort& input,
                          const OperatorContext& ctx) const override;
    const char* getName() const override { return "Brightness"; }

private:
    float brightness_;  // -1.0 ~ 1.0
};

// グレースケールオペレーター
class GrayscaleOperator : public SingleInputOperator {
public:
    GrayscaleOperator() = default;
    ViewPort applyToSingle(const ViewPort& input,
                          const OperatorContext& ctx) const override;
    const char* getName() const override { return "Grayscale"; }
};

// ボックスブラーオペレーター
class BoxBlurOperator : public SingleInputOperator {
public:
    explicit BoxBlurOperator(int radius) : radius_(radius > 0 ? radius : 1) {}
    ViewPort applyToSingle(const ViewPort& input,
                          const OperatorContext& ctx) const override;
    const char* getName() const override { return "BoxBlur"; }

private:
    int radius_;  // ブラー半径（1以上）
};

// アルファ調整オペレーター
class AlphaOperator : public SingleInputOperator {
public:
    explicit AlphaOperator(float alpha) : alpha_(alpha) {}
    ViewPort applyToSingle(const ViewPort& input,
                          const OperatorContext& ctx) const override;
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
    // originX/Y: 変換の中心点（入力画像座標系）
    // outputOffsetX/Y: 出力バッファ内でのレンダリング位置オフセット
    // outputWidth/Height: 出力サイズ（0以下の場合はコンテキストのcanvasSizeを使用）
    AffineOperator(const AffineMatrix& matrix,
                   double originX = 0, double originY = 0,
                   double outputOffsetX = 0, double outputOffsetY = 0,
                   int outputWidth = 0, int outputHeight = 0);

    ViewPort applyToSingle(const ViewPort& input,
                          const OperatorContext& ctx) const override;
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
    double originX_, originY_;
    double outputOffsetX_, outputOffsetY_;
    int outputWidth_, outputHeight_;
};

// ========================================================================
// 合成オペレーター
// 複数入力画像を重ね合わせる
// ========================================================================

class CompositeOperator : public NodeOperator {
public:
    CompositeOperator() = default;

    ViewPort apply(const std::vector<ViewPort>& inputs,
                  const OperatorContext& ctx) const override;

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
    static std::unique_ptr<NodeOperator> createAffineOperator(
        const AffineMatrix& matrix,
        double originX, double originY,
        double outputOffsetX, double outputOffsetY,
        int outputWidth, int outputHeight
    );

    // 合成オペレーターを生成
    static std::unique_ptr<NodeOperator> createCompositeOperator();
};

} // namespace FLEXIMG_NAMESPACE

#endif // FLEXIMG_OPERATORS_H
