# フィルタノード設計

フィルタ処理を行うノードクラスの設計を説明します。

## 概要

フィルタノードは種類ごとに独立したクラスとして実装されています。

```
Node (基底クラス)
└── FilterNodeBase (フィルタ共通基底)
    ├── BrightnessNode  - 明るさ調整
    ├── GrayscaleNode   - グレースケール変換
    ├── BoxBlurNode     - ボックスブラー
    └── AlphaNode       - アルファ調整
```

### 設計の目的

1. **型安全なパラメータ**: 各フィルタが専用のパラメータを持つ
2. **独立したメトリクス**: フィルタ種類別の性能計測が可能
3. **拡張性**: 新フィルタ追加時に既存コードを変更しない

---

## FilterNodeBase

全フィルタノードの共通基底クラス。

```cpp
class FilterNodeBase : public Node {
public:
    FilterNodeBase() { initPorts(1, 1); }  // 入力1、出力1

protected:
    // 派生クラスがオーバーライド
    virtual int computeInputMargin() const { return 0; }
    virtual int nodeTypeForMetrics() const = 0;

public:
    RenderResult pullProcess(const RenderRequest& request) override {
        int margin = computeInputMargin();
        RenderRequest inputReq = request.expand(margin);

        RenderResult input = upstreamNode(0)->pullProcess(inputReq);
        return process(std::move(input), request);
    }
};
```

### 責務

- 入力ポート/出力ポートの初期化（1:1）
- マージン拡大（`computeInputMargin()`）
- メトリクス記録（`nodeTypeForMetrics()`）
- `process()` への委譲

---

## 派生クラス

### BrightnessNode

```cpp
class BrightnessNode : public FilterNodeBase {
    float amount_ = 0.0f;  // -1.0〜1.0

protected:
    int nodeTypeForMetrics() const override { return NodeType::Brightness; }

    RenderResult process(RenderResult&& input, const RenderRequest& request) override {
        ImageBuffer working = std::move(input.buffer).toFormat(PixelFormatIDs::RGBA8_Straight);
        ImageBuffer output(working.width(), working.height(), PixelFormatIDs::RGBA8_Straight);
        filters::brightness(output.view(), working.view(), amount_);
        return RenderResult(std::move(output), input.origin);
    }
};
```

### BoxBlurNode

ブラーは入力マージンが必要な例。

```cpp
class BoxBlurNode : public FilterNodeBase {
    int radius_ = 5;

protected:
    int computeInputMargin() const override { return radius_; }
    int nodeTypeForMetrics() const override { return NodeType::BoxBlur; }

    RenderResult process(RenderResult&& input, const RenderRequest& request) override {
        // 1. フォーマット変換
        // 2. ブラー適用
        // 3. 要求範囲を切り出し（margin分を除去）
    }
};
```

---

## ピクセルフォーマット変換

各フィルタノードは `process()` 内で必要なフォーマット変換を行います。

```cpp
// ImageBuffer::toFormat() を使用
ImageBuffer working = std::move(input.buffer).toFormat(PixelFormatIDs::RGBA8_Straight);
```

**設計ポイント**:
- 同じフォーマットならムーブ（コピーなし）
- 異なるフォーマットなら変換
- 出力は変換戻しなし（次のノードが必要に応じて変換）

---

## NodeType とメトリクス

各フィルタは独立した `NodeType` を持ち、個別に計測されます。

```cpp
namespace NodeType {
    constexpr int Source = 0;
    constexpr int Transform = 1;
    constexpr int Composite = 2;
    constexpr int Output = 3;
    constexpr int Brightness = 4;
    constexpr int Grayscale = 5;
    constexpr int BoxBlur = 6;
    constexpr int Alpha = 7;
    constexpr int Count = 8;
}
```

WebUI側では `NODE_TYPES` 定義で一元管理されています。

---

## 新しいフィルタの追加手順

1. **C++側**
   - `FilterNodeBase` を継承したクラスを作成
   - `perf_metrics.h` の `NodeType` に追加
   - `bindings.cpp` に生成ロジックを追加

2. **WebUI側**
   - `app.js` の `NODE_TYPES` に定義を追加
   - UIは自動的に新ノードを表示

---

## ファイル構成

```
src/fleximg/nodes/
├── filter_node_base.h   # 共通基底クラス
├── brightness_node.h    # 明るさ調整
├── grayscale_node.h     # グレースケール
├── box_blur_node.h      # ボックスブラー
└── alpha_node.h         # アルファ調整
```
