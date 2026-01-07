# 部分ViewPort返却設計

## 概要

タイル分割処理時のメモリ効率を改善するため、各ノードが要求された範囲のうち実際に保有しているデータ量だけを返却できるようにする。

## 設計原則

> **要求された範囲のうち、実際に保有しているデータ量だけ返す**

| 要求 | 保有データとの重複 | 返却 |
|-----|------------------|-----|
| 幅10 | 幅10が重複 | 幅10 (完全) |
| 幅10 | 幅5が重複 | 幅5 (部分) |
| 幅10 | 幅0が重複 | 幅0 (空) |

「空」は特別なケースではなく、連続的な概念の端点として扱う。

## 動機

タイル分割が有効で元画像が小さい場合、多くのタイルが画像範囲外になる：

```
+--+--+--+--+--+
|  |  |  |  |  |  ← タイル (64x64 等)
+--+--+--+--+--+
|  |##|##|  |  |  ← ## = 元画像 (100x80 等)
+--+--+--+--+--+
|  |  |  |  |  |
+--+--+--+--+--+

空タイル: 画像と重ならないタイル
部分タイル: 画像と一部だけ重なるタイル
```

現状: 空タイルでもゼロ埋めバッファを確保して返却
改善後: 空タイルは width=0, height=0 で返却（メモリ確保なし）

## srcOrigin の扱い

部分返却では srcOrigin が必須：

```
要求: originX=100, width=20 (座標100〜119の範囲を要求)
画像: 実際には座標110〜115にのみデータあり

返却: width=6, srcOriginX=110
      ↑ このデータは座標110から始まることを示す
```

空返却 (width=0) でも srcOrigin を保持：
- コードパスの統一（特別処理不要）
- ループが0回回るだけで同じロジックが適用可能

## 影響範囲

### ImageEvalNode (起点)

```cpp
ViewPort ImageEvalNode::evaluate(const RenderRequest& request) {
    // 画像と要求範囲の交差を計算
    int imgMinX = -srcOriginX;  // 画像左端の基準相対座標
    int imgMaxX = imgMinX + image.width;
    int reqMinX = -request.originX;
    int reqMaxX = reqMinX + request.width;

    // 交差範囲
    int intersectMinX = std::max(imgMinX, reqMinX);
    int intersectMaxX = std::min(imgMaxX, reqMaxX);
    int intersectWidth = std::max(0, intersectMaxX - intersectMinX);

    if (intersectWidth == 0 || intersectHeight == 0) {
        // 空のViewPortを返す（メモリ確保なし）
        ViewPort empty;
        empty.width = 0;
        empty.height = 0;
        empty.srcOriginX = request.originX;  // origin情報は保持
        empty.srcOriginY = request.originY;
        return empty;
    }

    // 部分または完全なViewPortを返す
    // ...
}
```

### FilterEvalNode

```cpp
ViewPort FilterEvalNode::evaluate(const RenderRequest& request) {
    ViewPort input = upstream->evaluate(inputRequest);

    // 入力が空なら空を返す
    if (input.width == 0 || input.height == 0) {
        return input;  // そのまま返す（origin情報保持）
    }

    // 通常のフィルタ処理
    return operator_->apply({input}, request);
}
```

### AffineEvalNode

```cpp
ViewPort AffineEvalNode::evaluate(const RenderRequest& request) {
    ViewPort input = upstream->evaluate(inputRequest);

    if (input.width == 0 || input.height == 0) {
        return input;
    }

    return operator_->apply({input}, request);
}
```

### CompositeEvalNode

```cpp
ViewPort CompositeEvalNode::evaluate(const RenderRequest& request) {
    std::vector<ViewPort> inputs;
    for (auto& upstream : upstreams) {
        ViewPort vp = upstream->evaluate(request);
        inputs.push_back(std::move(vp));
    }

    // CompositeOperator内で空入力をスキップ
    return operator_->apply(inputs, request);
}
```

### CompositeOperator

```cpp
ViewPort CompositeOperator::apply(const std::vector<ViewPort>& inputs,
                                  const RenderRequest& request) const {
    ViewPort output(request.width, request.height, ...);

    for (const auto& input : inputs) {
        if (input.width == 0 || input.height == 0) {
            continue;  // 空入力はスキップ
        }
        // 合成処理
    }

    return output;
}
```

### OutputEvalNode / render()

```cpp
// タイル処理ループ
for (each tile) {
    ViewPort tileResult = pipeline->evaluate(tileRequest);

    if (tileResult.width == 0 || tileResult.height == 0) {
        continue;  // 空タイルはコピーをスキップ
    }

    // 出力バッファへコピー
}
```

## isValid() の扱い

現在の `ViewPort::isValid()`:
```cpp
bool isValid() const { return data != nullptr && width > 0 && height > 0; }
```

空ViewPort (width=0) は `isValid() == false` になる。
これは意図通り。空でもorigin情報は有効だが、描画データとしては無効。

## 実装順序

1. **ImageEvalNode**: 範囲判定と部分/空ViewPort返却
2. **FilterEvalNode**: 空入力時の早期リターン
3. **AffineEvalNode**: 空入力時の早期リターン
4. **CompositeEvalNode/Operator**: 空入力のスキップ
5. **render()**: 空タイルのコピースキップ
6. **テスト**: 小さい画像 + 大きいキャンバス + タイル分割で検証

## 期待される効果

- 空タイルでのメモリ確保が不要
- 空タイルでのゼロ埋め処理が不要
- 空タイルでのオペレータ処理がスキップ
- 特に「小さい画像 + 大きいキャンバス + タイル分割」で効果大
