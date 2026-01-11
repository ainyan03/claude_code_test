# 案B: 出力に有効範囲情報を付加

## 概要

RenderResult に「有効ピクセル範囲」の情報を追加し、
CompositeNode がその範囲外を透明として扱えるようにする。

## 設計

### RenderResult の拡張

```cpp
struct RenderResult {
    ImageBuffer buffer;
    Point origin;

    // 新規: 有効範囲（バッファ内座標）
    // nullopt = バッファ全体が有効
    std::optional<Rect> validRegion;

    bool isValid() const { return buffer.isValid(); }

    // 指定座標が有効範囲内かチェック
    bool isValidAt(int x, int y) const {
        if (!validRegion) return true;
        return x >= validRegion->x && x < validRegion->x + validRegion->w
            && y >= validRegion->y && y < validRegion->y + validRegion->h;
    }
};
```

### AffineNode の変更

```cpp
RenderResult process(RenderResult&& input, const RenderRequest& request) override {
    // ... アフィン変換処理 ...

    RenderResult result(std::move(output), request.origin);

    // 有効範囲を設定（DDA で書き込んだ範囲）
    result.validRegion = computeValidRegion(request, input);

    return result;
}
```

### CompositeNode の変更

```cpp
// ブレンド時に有効範囲を考慮
if (inputResult.validRegion) {
    // 有効範囲のみブレンド
    blendWithRegion(canvas, inputResult, inputResult.validRegion.value());
} else {
    // 従来通り全体ブレンド
    blend::onto(...);
}
```

## メリット

- **メモリ効率**: フォーマット変換不要、元のバイト数を維持
- **精密**: ピクセル単位の有効/無効を表現可能
- **柔軟**: 任意のノードが有効範囲を設定可能

## デメリット

- **API変更**: RenderResult の構造変更
- **複雑度**: CompositeNode のブレンド処理が複雑化
- **オーバーヘッド**: 有効範囲チェックのコスト

## 考慮事項

### 有効範囲の表現方法

1. **矩形（Rect）**: シンプルだが、回転後は矩形に収まらない
2. **平行四辺形（4頂点）**: 正確だが、ブレンド処理が複雑
3. **ビットマスク**: 完全に正確だが、メモリコスト大

### 矩形の限界

```
回転後の有効範囲:
    ┌─────────────────┐
    │      /\         │  ← 矩形で囲むと無効領域が含まれる
    │     /  \        │
    │    /    \       │
    │   /______\      │
    │                 │
    └─────────────────┘
```

矩形では回転画像の斜め部分を正確に表現できない。
ただし、タイル分割との組み合わせで実用上は問題ない可能性あり。

### タイル分割との相性

タイル分割時、各タイルの有効範囲は比較的単純な形状になるため、
矩形で十分な場合が多い。

## 実装コスト

中〜高。API変更と CompositeNode の大幅な修正が必要。

## 関連

- `InputRegion` 構造体（AffineNode 内で類似概念あり）
- タイル分割処理
