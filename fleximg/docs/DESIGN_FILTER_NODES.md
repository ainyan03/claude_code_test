# フィルタノード設計

フィルタ処理を行うノードクラスの設計を説明します。

## 概要

フィルタノードは種類ごとに独立したクラスとして実装されています。

```
Node (基底クラス)
├── FilterNodeBase (フィルタ共通基底)
│   ├── BrightnessNode      - 明るさ調整
│   ├── GrayscaleNode       - グレースケール変換
│   └── AlphaNode           - アルファ調整
│
└── 分離型ブラー（独立実装、ガウシアン近似対応）
    ├── HorizontalBlurNode  - 水平ブラー
    └── VerticalBlurNode    - 垂直ブラー
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
    RenderResponse pullProcess(const RenderRequest& request) override {
        int margin = computeInputMargin();
        RenderRequest inputReq = request.expand(margin);

        RenderResponse input = upstreamNode(0)->pullProcess(inputReq);
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

## パラメータ一覧

各フィルタノードのパラメータと有効範囲です。

| フィルタ | パラメータ | 型 | 範囲 | デフォルト | 説明 |
|---------|----------|-----|------|----------|------|
| BrightnessNode | amount | float | -1.0〜1.0 | 0.0 | 明るさ調整量。正で明るく、負で暗く |
| GrayscaleNode | - | - | - | - | パラメータなし |
| AlphaNode | scale | float | 0.0〜1.0 | 1.0 | アルファスケール。0.5で50%の不透明度 |
| HorizontalBlurNode | radius | int | 0〜127 | 5 | ブラー半径。0でスルー出力 |
|  | passes | int | 1〜3 | 1 | ブラー適用回数。3でガウシアン近似 |
| VerticalBlurNode | radius | int | 0〜127 | 5 | ブラー半径。0でスルー出力 |
|  | passes | int | 1〜3 | 1 | ブラー適用回数。3でガウシアン近似 |

### 設定例

```cpp
BrightnessNode brightness;
brightness.setAmount(0.2f);   // 20%明るく

AlphaNode alpha;
alpha.setScale(0.5f);         // 50%の不透明度

// ガウシアン近似ブラー
HorizontalBlurNode hblur;
hblur.setRadius(6);
hblur.setPasses(3);

VerticalBlurNode vblur;
vblur.setRadius(6);
vblur.setPasses(3);
```

---

## 派生クラス

### BrightnessNode

```cpp
class BrightnessNode : public FilterNodeBase {
    float amount_ = 0.0f;  // -1.0〜1.0

protected:
    int nodeTypeForMetrics() const override { return NodeType::Brightness; }

    RenderResponse process(RenderResponse&& input, const RenderRequest& request) override {
        ImageBuffer working = std::move(input.buffer).toFormat(PixelFormatIDs::RGBA8_Straight);
        ImageBuffer output(working.width(), working.height(), PixelFormatIDs::RGBA8_Straight);
        filters::brightness(output.view(), working.view(), amount_);
        return RenderResponse(std::move(output), input.origin);
    }
};
```

### HorizontalBlurNode / VerticalBlurNode（推奨）

分離型ブラーフィルタ。**ガウシアン近似に対応**。

```cpp
class HorizontalBlurNode : public Node {
    int radius_ = 5;
    int passes_ = 1;  // 1〜3

protected:
    // pull型: マージンを確保して上流に要求、下流には元サイズで返却
    // ※実装では onPullProcess() をオーバーライド（Template Methodパターン）
    RenderResponse onPullProcess(const RenderRequest& request) override {
        // 上流への要求（マージン付き）
        int totalMargin = radius_ * passes_;
        RenderRequest inputReq;
        inputReq.width = request.width + totalMargin * 2;
        RenderResponse input = upstreamNode(0)->pullProcess(inputReq);

        // passes回、水平ブラーを適用（内部で拡張）
        for (int pass = 0; pass < passes_; pass++) {
            applyHorizontalBlur(srcView, -radius_, output);
        }

        // 中央部分をクロップして元のサイズで返却
        return RenderResponse(cropToCenter(buffer, request.width), request.origin);
    }

    // push型: 入力を拡張して下流に配布
    // ※実装では onPushProcess() をオーバーライド
    void onPushProcess(RenderResponse&& input, const RenderRequest& request) override {
        // passes回ブラーを適用（各パスで拡張）
        // 拡張された結果を下流に配布
    }
};
```

**特徴**:
- **マルチパス対応**: passes=3で3回ブラーを適用し、ガウシアン分布に近似
- **中心極限定理**: 複数回のボックスブラーでガウシアンに収束
- **パイプライン方式**: 各パスが独立して処理され、境界処理も独立
- **pull型**: マージン付き要求、元サイズで返却（FilterNodeBaseパターン準拠）
- **push型**: 入力拡張、拡張結果を配布（width → width + radius*2*passes）
- **pull/push両対応**: RendererNodeの上流・下流どちらでも使用可能
- **スキャンライン最適化**: 1行ずつ処理（HorizontalBlurNode）

**使用例**:
```cpp
// ガウシアン近似ブラー
HorizontalBlurNode hblur;
hblur.setRadius(6);
hblur.setPasses(3);  // 3パスでガウシアン近似

VerticalBlurNode vblur;
vblur.setRadius(6);
vblur.setPasses(3);

// パイプライン構築
src >> hblur >> vblur >> sink;
```

**垂直ブラーの実装**:
- パイプライン方式: 各パスが独立したステージとして処理
- メモリ消費: 各ステージ (radius×2+1)×width×4 + width×16 bytes
- 「3パス×1ノード」と「1パス×3ノード直列」が同等の結果

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
定義は `src/fleximg/perf_metrics.h` を参照。

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
├── filter_node_base.h      # 共通基底クラス
├── brightness_node.h       # 明るさ調整
├── grayscale_node.h        # グレースケール
├── horizontal_blur_node.h  # 水平ブラー（ガウシアン対応）
├── vertical_blur_node.h    # 垂直ブラー（ガウシアン対応）
└── alpha_node.h            # アルファ調整
```

## 関連ドキュメント

- [ideas/archived/IDEA_GAUSSIAN_BLUR_UPGRADE.md](ideas/archived/IDEA_GAUSSIAN_BLUR_UPGRADE.md) - ガウシアン近似の理論と実装提案
- [CHANGELOG.md](../CHANGELOG.md#2340---2026-01-18) - v2.34.0での追加機能
- [DESIGN_RENDERER_NODE.md](DESIGN_RENDERER_NODE.md) - originとマージンの扱い
