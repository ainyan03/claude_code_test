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

### OperatorContext（実行コンテキスト）

オペレーター実行時に必要な共通情報を保持：

```cpp
struct OperatorContext {
    int canvasWidth;
    int canvasHeight;
    double dstOriginX;  // 出力基準点X
    double dstOriginY;  // 出力基準点Y

    // 将来の拡張用
    // - タイル情報
    // - キャッシュ参照
    // - パフォーマンス計測
};
```

### NodeOperator（基底クラス）

```cpp
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
};
```

### SingleInputOperator（単一入力用基底クラス）

フィルタ等の1入力オペレーター実装を簡潔にするための基底クラス：

```cpp
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
    explicit AffineOperator(const AffineMatrix& matrix);
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
    // 事前計算された逆行列（固定小数点）
    int32_t fixedInvA_, fixedInvB_, fixedInvC_, fixedInvD_;
    int32_t fixedInvTx_, fixedInvTy_;
};
```

### CompositeOperator

```cpp
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
```

---

## NodeGraphEvaluator への統合

### オペレーターファクトリー

ノード情報からオペレーターを生成：

```cpp
class OperatorFactory {
public:
    static std::unique_ptr<NodeOperator> create(const GraphNode& node);
};

// 使用例
std::unique_ptr<NodeOperator> op = OperatorFactory::create(node);
ViewPort result = op->apply(inputs, ctx);
```

### 評価ループの簡略化

現在の `evaluateNode()` 内の分岐を統一：

```cpp
// Before (現在)
if (node->type == "filter") {
    result = processor.applyFilter(inputImage, filterType, filterParams);
} else if (node->type == "composite") {
    result = processor.mergeImages(imagePtrs, dstOriginX, dstOriginY);
} else if (node->type == "affine") {
    result = processor.applyTransform(inputImage, matrix, ...);
}

// After (新設計)
std::unique_ptr<NodeOperator> op = OperatorFactory::create(*node);
result = op->apply(inputs, ctx);
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

- [TODO.md](TODO.md): タスク管理
- [DESIGN_2PASS_EVALUATION.md](DESIGN_2PASS_EVALUATION.md): 2パス評価システム設計
