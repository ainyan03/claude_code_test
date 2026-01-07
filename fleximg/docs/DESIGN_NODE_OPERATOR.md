# ノードオペレーター統一設計

## 背景

現在の実装では、ノードタイプによって異なる実装方式が混在している：

| ノードタイプ | 現在の実装場所 | 問題点 |
|-------------|----------------|--------|
| フィルタ | `ImageFilter` 派生クラス | ◎ 統一的 |
| 合成 | `ImageProcessor::mergeImages()` | △ メソッド直接呼び出し |
| アフィン変換 | `ImageProcessor::applyTransform()` | △ メソッド直接呼び出し |

**課題**:
1. `node_graph.cpp` 内でノードタイプごとに異なる呼び出し方式が必要
2. 新規ノードタイプ追加時に `node_graph.cpp` の変更が必須
3. 合成・アフィン変換のユニットテストが困難
4. `ImageProcessor` が肥大化し、責務が不明確

## 設計目標

1. **統一インターフェース**: すべてのノード処理を同一の方法で呼び出し可能にする
2. **拡張性**: 新規ノードタイプはクラス追加のみで対応
3. **テスト容易性**: 各オペレーターを独立してテスト可能
4. **段階的移行**: 既存コードを破壊せず段階的に移行

---

## クラス階層

```
NodeOperator (抽象基底クラス)
│
├── SingleInputOperator (1入力専用の便利基底クラス)
│   ├── BrightnessOperator
│   ├── GrayscaleOperator
│   ├── BoxBlurOperator
│   ├── AlphaOperator
│   └── AffineOperator
│
└── (直接継承: 複数入力オペレーター)
    └── CompositeOperator
```

---

## インターフェース定義

### RenderRequest（レンダリング要求）

オペレーター実行時に必要な情報を保持（タイル分割処理対応）：

```cpp
struct RenderRequest {
    int x, y;           // 要求範囲の左上座標（基準相対座標）
    int width, height;  // 要求範囲のサイズ
    double originX;     // バッファ内での基準点X位置
    double originY;     // バッファ内での基準点Y位置

    // 領域拡大（フィルタのカーネル半径対応）
    RenderRequest expand(int radius) const;
};
```

### NodeOperator（基底クラス）

```cpp
class NodeOperator {
public:
    virtual ~NodeOperator() = default;

    // メイン処理: 入力群からViewPortを生成
    virtual ViewPort apply(const std::vector<ViewPort>& inputs,
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
};
```

### SingleInputOperator（単一入力用基底クラス）

フィルタ等の1入力オペレーター実装を簡潔にするための基底クラス：

```cpp
class SingleInputOperator : public NodeOperator {
public:
    // NodeOperator::apply を実装
    ViewPort apply(const std::vector<ViewPort>& inputs,
                  const RenderRequest& request) const override final {
        if (inputs.empty()) {
            throw std::invalid_argument("SingleInputOperator requires at least 1 input");
        }
        return applyToSingle(inputs[0], request);
    }

    // 派生クラスはこちらを実装
    virtual ViewPort applyToSingle(const ViewPort& input,
                                   const RenderRequest& request) const = 0;

    int getMinInputCount() const override { return 1; }
    int getMaxInputCount() const override { return 1; }
};
```

---

## 各オペレーターの設計

### フィルタオペレーター群

既存の `ImageFilter` 派生クラスを `SingleInputOperator` 派生に変更：

```cpp
class BrightnessOperator : public SingleInputOperator {
public:
    explicit BrightnessOperator(float brightness);
    ViewPort applyToSingle(const ViewPort& input,
                          const OperatorContext& ctx) const override;
    const char* getName() const override { return "Brightness"; }
private:
    float brightness_;
};
```

### AffineOperator

```cpp
class AffineOperator : public SingleInputOperator {
public:
    // ファクトリ関数で生成（パラメータが多いため）
    // inputSrcOriginX/Y: 入力の基準相対座標
    // outputOriginX/Y: 出力バッファ内での基準点位置
    static std::unique_ptr<NodeOperator> create(
        const AffineMatrix& matrix,
        double inputSrcOriginX, double inputSrcOriginY,
        double outputOriginX, double outputOriginY,
        int outputWidth, int outputHeight);

    ViewPort applyToSingle(const ViewPort& input,
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
    // 事前計算された逆行列（固定小数点）
    int32_t fixedInvA_, fixedInvB_, fixedInvC_, fixedInvD_;
    int32_t fixedInvTx_, fixedInvTy_;
    double inputSrcOriginX_, inputSrcOriginY_;
    double outputOriginX_, outputOriginY_;
    int outputWidth_, outputHeight_;
};
```

### CompositeOperator

```cpp
class CompositeOperator : public NodeOperator {
public:
    CompositeOperator() = default;

    ViewPort apply(const std::vector<ViewPort>& inputs,
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
};
```

---

## NodeGraphEvaluator への統合

### オペレーターファクトリー

ノードタイプごとにオペレーターを生成：

```cpp
class OperatorFactory {
public:
    // フィルタオペレーター生成
    static std::unique_ptr<NodeOperator> createFilterOperator(
        const std::string& filterType,
        const std::vector<float>& params);

    // アフィン変換オペレーター生成
    static std::unique_ptr<NodeOperator> createAffineOperator(
        const AffineMatrix& matrix,
        double inputSrcOriginX, double inputSrcOriginY,
        double outputOriginX, double outputOriginY,
        int outputWidth, int outputHeight);

    // 合成オペレーター生成
    static std::unique_ptr<NodeOperator> createCompositeOperator();
};
```

### 評価ループの簡略化

`EvaluationNode` クラスが各ノードタイプの処理をカプセル化：

```cpp
// 旧: ImageProcessor メソッドを直接呼び出し
if (node->type == "filter") {
    result = processor.applyFilter(inputImage, filterType, filterParams);
} else if (node->type == "composite") {
    result = processor.mergeImages(imagePtrs, dstOriginX, dstOriginY);
}

// 新: EvaluationNode による統一的な評価
// 各ノードタイプは EvaluationNode 派生クラスとして実装
ViewPort result = evalNode->evaluate(request, context);
```

---

## 移行計画

### Phase 1: 基盤整備 ✅
- [x] `OperatorContext` 構造体の定義
- [x] `NodeOperator` 基底クラスの定義
- [x] `SingleInputOperator` 基底クラスの定義
- [x] `operators.h` / `operators.cpp` ファイル作成

### Phase 2: フィルタオペレーター移行 ✅
- [x] `BrightnessOperator` 実装（既存 `BrightnessFilter` をラップ）
- [x] `GrayscaleOperator` 実装
- [x] `BoxBlurOperator` 実装
- [x] `AlphaOperator` 実装
- [x] `OperatorFactory::createFilterOperator()` 実装
- [x] ビルド確認

### Phase 3: アフィン・合成オペレーター実装 ✅
- [x] `AffineOperator` 実装（`ImageProcessor::applyTransform` から移植）
- [x] `CompositeOperator` 実装（`ImageProcessor::mergeImages` から移植）
- [x] `OperatorFactory` に `createAffineOperator()`, `createCompositeOperator()` 追加
- [x] ビルド確認

### Phase 4: NodeGraphEvaluator 統合 ✅
- [x] `evaluateNode()` をオペレーター呼び出しに変更
- [x] `evaluateNodeWithRequest()` をオペレーター呼び出しに変更
- [x] `evaluateGraph()` 内の `mergeImages` をオペレーター呼び出しに変更
- [x] 動作確認（ビルド成功）

### Phase 5: クリーンアップ ✅
- [x] `ImageProcessor` クラスを完全削除（`image_processor.h/cpp`）
- [x] `FilterRegistry` クラスを完全削除（`filter_registry.h/cpp`）
- [x] `NodeGraphEvaluator` から `processor` メンバーを削除
- [x] `build.sh` を更新
- [x] ドキュメント更新

### Phase 6: フィルタ処理のオペレーター統合 ✅
- [x] `BrightnessOperator` にフィルタ処理を直接実装
- [x] `GrayscaleOperator` にフィルタ処理を直接実装
- [x] `BoxBlurOperator` にフィルタ処理を直接実装
- [x] `AlphaOperator` にフィルタ処理を直接実装
- [x] `operators.h` から `#include "filters.h"` を削除
- [x] `filters.h/cpp` を削除（約190行削減）
- [x] `build.sh` を更新
- [x] ビルド・動作確認

**結果:**
- 二重クラス階層（`ImageFilter` + `NodeOperator`）を解消
- オペレーターが唯一のフィルタ処理クラスに
- `ImageFilter` 基底クラスを完全削除

---

## 将来の拡張

この設計により、以下のノードタイプが容易に追加可能：

- **マスク合成**: `MaskCompositeOperator` (3入力: 背景, 前景, マスク)
- **ブレンドモード**: `BlendOperator` (2入力 + モードパラメータ)
- **クロップ**: `CropOperator` (1入力 + 矩形パラメータ)
- **色調補正**: `HueSaturationOperator` (1入力)

---

## 関連ドキュメント

- [TODO.md](../TODO.md): タスク管理
- [DESIGN_PIPELINE_EVALUATION.md](DESIGN_PIPELINE_EVALUATION.md): パイプライン評価システム設計
- [DESIGN_TILE_COORDINATE_SYSTEM.md](DESIGN_TILE_COORDINATE_SYSTEM.md): タイル座標系設計
