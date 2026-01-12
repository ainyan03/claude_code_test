# フォーマット交渉（Format Negotiation）

## 概要

ノード間でピクセルフォーマットの要求を問い合わせ、パイプライン全体で最適なフォーマットを自動決定する仕組み。

## 動機

現状は「受け取った側が自分に適したフォーマットに変換する」方式だが、以下の非効率がある:

1. **冗長な変換**: A→B→C で、Aが出力をRGB565に変換 → Bが内部処理でRGBA16に変換 → Cが最終出力でRGB565に変換
2. **情報損失**: 高精度フォーマットから低精度に変換後、再度高精度が必要になるケース

## 提案

### 下流からの要求収集

```cpp
struct FormatPreference {
    PixelFormatID preferred;           // 最も望ましいフォーマット
    std::vector<PixelFormatID> acceptable;  // 許容可能なフォーマット
    bool canConvert;                   // 自前で変換可能か
};

// Nodeインターフェース
virtual FormatPreference queryFormatPreference() const;
```

### 適用シナリオ

#### 1. Renderer下流（DistributorNode → 複数Sink）

```
DistributorNode
    │
    ├→ SinkNode1 (preferred: RGB565)
    ├→ SinkNode2 (preferred: RGBA8888)
    └→ SinkNode3 (preferred: RGB565)
```

DistributorNodeが下流を問い合わせ:
- 2/3がRGB565を要求 → RGB565で出力し、Sink2だけが変換

#### 2. Renderer上流（SourceNode → フィルタ → Renderer）

```
SourceNode (RGB565)
    │
    ▼
BrightnessNode (requires: RGBA16)
    │
    ▼
GrayscaleNode (requires: RGBA8 or RGBA16)
    │
    ▼
RendererNode
```

上流に向かって要求を伝播:
- GrayscaleNode → BrightnessNode: 「RGBA16でOK」
- BrightnessNode → SourceNode: 「RGBA16で出力希望」
- SourceNode: RGB565 → RGBA16 変換して出力

### 実装案

#### Phase 1: 静的宣言

各ノードが静的に「入力希望」「出力可能」フォーマットを宣言:

```cpp
class BrightnessNode : public FilterNodeBase {
    static constexpr PixelFormatID preferredInput = RGBA16_Premultiplied;
    static constexpr PixelFormatID preferredOutput = RGBA8_Straight;
};
```

#### Phase 2: 動的交渉

パイプライン構築時に双方向で交渉:

```cpp
// 1. 下流から上流へ要求を収集
void Node::collectUpstreamRequirements();

// 2. 上流から下流へ決定を通知
void Node::propagateDecision(PixelFormatID decided);
```

## 考慮事項

- **パフォーマンス**: 交渉オーバーヘッド vs 変換コスト
- **複雑性**: 循環参照や競合する要求の解決
- **メモリ**: 中間バッファのフォーマット選択がピークメモリに影響

## 関連

- DistributorNode（複数Sink対応）で最初の適用ケース
- 将来的にはSourceNode〜Renderer間にも適用可能
