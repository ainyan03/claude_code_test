# 逐次合成設計

## 概要

CompositeEvalNodeのメモリ効率を改善するため、「全入力を収集してから一括合成」から「入力を1つずつ取得して逐次合成」に変更する。

## 動機

現状の問題:
```
上流が5つある場合:
┌──────────────────────────────────────────────┐
│ input[0] │ input[1] │ input[2] │ input[3] │ input[4] │
└──────────────────────────────────────────────┘
                      ↓
              CompositeOperator.apply()
                      ↓
                   output

メモリ使用量: O(n) バッファ同時保持
```

改善後:
```
┌─────────┐
│ canvas  │ ← 出力バッファ（1つだけ）
└─────────┘
    + input[0] → 合成 → input[0] 解放
    + input[1] → 合成 → input[1] 解放
    ...

メモリ使用量: O(2) = canvas + 現在の入力1つ
```

## 設計

### 初期キャンバスの扱い

最初の非空入力に対して、要求範囲との関係で分岐:

| 条件 | 処理 |
|------|------|
| 完全カバー | そのままmoveしてキャンバスに |
| 部分オーバーラップ | 透明キャンバス確保 + memcpyでコピー |
| 空 | スキップして次の入力へ |

### 最初の入力の最適化

部分オーバーラップ時、最初の入力は **ブレンドではなくmemcpy** で効率化:

```
透明キャンバス (0,0,0,0) + 入力A = 入力A

ブレンド計算: out = src + dst * (1 - srcAlpha)
透明dstの場合: out = src + 0 = src  ← 計算不要、コピーで済む
```

2枚目以降は既存データがあるためブレンド処理が必要。

### インターフェース変更

**CompositeOperator**

既存（変更なし、後方互換性のため保持）:
```cpp
ViewPort apply(const std::vector<ViewPort>& inputs,
               const RenderRequest& request) const override;
```

新規追加:
```cpp
// 透明キャンバスを作成
ViewPort createCanvas(const RenderRequest& request) const;

// キャンバスに入力を合成（最初の入力用、memcpy最適化）
void blendFirst(ViewPort& canvas, const ViewPort& input) const;

// キャンバスに入力を合成（2枚目以降、ブレンド処理）
void blendOnto(ViewPort& canvas, const ViewPort& input) const;
```

**CompositeEvalNode**

```cpp
ViewPort CompositeEvalNode::evaluate(const RenderRequest& request,
                                     const RenderContext& context) {
    ViewPort canvas;
    bool canvasInitialized = false;
    bool isFirstBlend = true;  // 最初のブレンドかどうか

    for (auto& upstream : upstreams_) {
        ViewPort input = upstream->evaluate(request, context);

        // 空入力はスキップ
        if (input.width == 0 || input.height == 0) {
            continue;
        }

        if (!canvasInitialized) {
            // 最初の非空入力
            if (coversFullRequest(input, request)) {
                // 完全カバー → moveでキャンバスに
                canvas = std::move(input);
            } else {
                // 部分オーバーラップ → 透明キャンバス確保 + コピー
                canvas = compositeOp_->createCanvas(request);
                compositeOp_->blendFirst(canvas, input);
            }
            canvasInitialized = true;
            isFirstBlend = false;
        } else {
            // 2枚目以降 → ブレンド処理
            compositeOp_->blendOnto(canvas, input);
        }
    }

    // 全ての入力が空だった場合
    if (!canvasInitialized) {
        ViewPort empty;
        empty.srcOriginX = -request.originX;
        empty.srcOriginY = -request.originY;
        return empty;
    }

    return canvas;
}
```

### 完全カバー判定

```cpp
bool coversFullRequest(const ViewPort& vp, const RenderRequest& request) {
    // 要求範囲の左上（基準点相対座標）
    float reqLeft = -request.originX;
    float reqTop = -request.originY;
    float reqRight = reqLeft + request.width;
    float reqBottom = reqTop + request.height;

    // ViewPortの範囲
    float vpLeft = vp.srcOriginX;
    float vpTop = vp.srcOriginY;
    float vpRight = vpLeft + vp.width;
    float vpBottom = vpTop + vp.height;

    // ViewPortが要求範囲を完全に含むか
    return (vpLeft <= reqLeft && vpRight >= reqRight &&
            vpTop <= reqTop && vpBottom >= reqBottom);
}
```

### blendFirst の実装（memcpy最適化）

```cpp
void CompositeOperator::blendFirst(ViewPort& canvas, const ViewPort& input) const {
    // 入力の位置をキャンバス座標に変換
    int offsetX = static_cast<int>(input.srcOriginX - canvas.srcOriginX);
    int offsetY = static_cast<int>(input.srcOriginY - canvas.srcOriginY);

    // クリッピング
    int srcStartX = std::max(0, -offsetX);
    int srcStartY = std::max(0, -offsetY);
    int dstStartX = std::max(0, offsetX);
    int dstStartY = std::max(0, offsetY);
    int copyWidth = std::min(input.width - srcStartX, canvas.width - dstStartX);
    int copyHeight = std::min(input.height - srcStartY, canvas.height - dstStartY);

    if (copyWidth <= 0 || copyHeight <= 0) return;

    // 行単位でmemcpy（ブレンド計算なし）
    for (int y = 0; y < copyHeight; y++) {
        const auto* src = input.getPixelPtr<uint16_t>(srcStartX, srcStartY + y);
        auto* dst = canvas.getPixelPtr<uint16_t>(dstStartX, dstStartY + y);
        std::memcpy(dst, src, copyWidth * 4 * sizeof(uint16_t));
    }
}
```

### blendOnto の実装（ブレンド処理）

既存の合成ロジックを流用。Premultiplied alpha のブレンド:
```
out = src + dst * (1 - srcAlpha)
```

## ピクセルフォーマットの考慮

- キャンバスは `RGBA16_Premultiplied` で作成
- 入力が異なるフォーマットの場合は変換が必要
- 現状のCompositeOperatorと同様の変換処理を維持

## 期待される効果

| ケース | 現状 | 改善後 |
|--------|------|--------|
| 入力5つ、各タイル64x64 | 5 * 64 * 64 * 8 = 160KB | 2 * 64 * 64 * 8 = 32KB |
| 入力10つ | 320KB | 32KB |

入力数が増えるほど効果が大きい。

## 実装順序

1. CompositeOperatorに新メソッド追加（createCanvas, blendFirst, blendOnto）
2. CompositeEvalNodeを逐次合成方式に変更
3. テスト実行・動作確認
4. 既存のapply()は後方互換性のため保持（将来的に削除検討）
