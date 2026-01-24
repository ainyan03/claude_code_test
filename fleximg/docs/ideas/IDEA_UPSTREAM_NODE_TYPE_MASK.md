# 上流ノードタイプのビットマスク伝播

**ステータス**: 構想段階

**用途**: アフィン変換の入力要求分割時の分割要否判定

## 概要

パイプライン構築時に上流ノードの構成をビットマスクで伝播させ、処理の最適化判断に活用する。

## 背景

アフィン変換の入力要求分割など、上流の構成によって最適な処理が異なるケースがある。

- 上流が SourceNode のみ → 分割不要（画像の切り出しは軽量）
- 上流にブラーフィルタ等の重いフィルタ → 分割推奨（無駄な処理を削減）

## ビットマスク定義

### カテゴリベースのグループ化

現在の NodeType（8種）を処理コスト観点で4カテゴリに分類:

```cpp
namespace NodeTypeMask {
    constexpr uint32_t NONE         = 0;
    constexpr uint32_t IMAGE        = 1 << 0;  // SourceNode（画像切り出し）
    constexpr uint32_t LIGHT_FILTER = 1 << 1;  // Brightness, Grayscale, Alpha
    constexpr uint32_t TRANSFORM    = 1 << 2;  // AffineNode（アフィン変換）
    constexpr uint32_t HEAVY_FILTER = 1 << 3;  // HorizontalBlur, VerticalBlur 等
    constexpr uint32_t COMPOSITE    = 1 << 4;  // CompositeNode
}
```

### ノードタイプとマスクの対応

| ノードクラス | NodeType | マスク |
|-------------|----------|--------|
| SourceNode | Source | IMAGE |
| BrightnessNode | Brightness | LIGHT_FILTER |
| GrayscaleNode | Grayscale | LIGHT_FILTER |
| AlphaNode | Alpha | LIGHT_FILTER |
| HorizontalBlurNode | HorizontalBlur | HEAVY_FILTER |
| VerticalBlurNode | VerticalBlur | HEAVY_FILTER |
| AffineNode | Affine | TRANSFORM |
| CompositeNode | Composite | COMPOSITE |

## 伝播の仕組み

### Node 基底クラスへの追加

```cpp
class Node {
protected:
    uint32_t upstreamMask_ = 0;

public:
    // 各派生クラスでオーバーライド
    virtual uint32_t getOwnMask() const = 0;

    // 上流マスクの取得
    uint32_t getUpstreamMask() const { return upstreamMask_; }

    // 準備フェーズで伝播
    void pullPrepare() override {
        upstreamMask_ = 0;
        for (Node* input : inputs_) {
            if (input) {
                input->pullPrepare();
                upstreamMask_ |= input->upstreamMask_ | input->getOwnMask();
            }
        }
    }
};
```

### 派生クラスでの実装例

```cpp
class SourceNode : public Node {
public:
    uint32_t getOwnMask() const override {
        return NodeTypeMask::IMAGE;
    }
};

class HorizontalBlurNode : public Node {
public:
    uint32_t getOwnMask() const override {
        return NodeTypeMask::HEAVY_FILTER;
    }
};

class VerticalBlurNode : public Node {
public:
    uint32_t getOwnMask() const override {
        return NodeTypeMask::HEAVY_FILTER;
    }
};

class AffineNode : public Node {
public:
    uint32_t getOwnMask() const override {
        return NodeTypeMask::TRANSFORM;
    }
};
```

## 活用例

### アフィン変換での分割判定

```cpp
bool AffineNode::shouldSplit() const {
    // 上流が IMAGE のみなら分割不要
    if (upstreamMask_ == 0) return false;

    // 上流に重いフィルタがあれば分割推奨
    return (upstreamMask_ & NodeTypeMask::HEAVY_FILTER) != 0;
}
```

### 将来の拡張例

```cpp
// メモリ確保戦略の選択
ImageAllocator* Node::selectAllocator() const {
    if (upstreamMask_ & NodeTypeMask::HEAVY_FILTER) {
        return &poolAllocator;  // 再利用を優先
    }
    return &defaultAllocator;
}

// タイル分割戦略の選択
TileConfig Node::selectTileConfig() const {
    if (upstreamMask_ & NodeTypeMask::TRANSFORM) {
        return TileConfig{64, 64};  // 小さめのタイル
    }
    return TileConfig{256, 256};  // 大きめのタイル
}
```

## 実装ステップ

1. [ ] NodeTypeMask namespace を追加（common.h または新規ファイル）
2. [ ] Node 基底クラスに upstreamMask_ と getOwnMask() を追加
3. [ ] pullPrepare() でマスク伝播を実装
4. [ ] 各派生クラスで getOwnMask() をオーバーライド
5. [ ] AffineNode::shouldSplit() で活用

## 設計上の考慮点

### カテゴリ粒度

- 現状は処理コスト観点で4カテゴリに集約
- 必要に応じて NodeType と1:1対応にも拡張可能

### 循環参照との関係

- PrepareStatus と併用して循環検出
- pullPrepare() で両方の情報を伝播

### オーバーヘッド

- ビットマスクは軽量（uint32_t の OR 演算のみ）
- 準備フェーズで一度だけ計算

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/node.h` | Node 基底クラス |
| `src/fleximg/perf_metrics.h` | NodeType 定義（参考） |
