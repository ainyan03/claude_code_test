# タイル効率的評価 設計ドキュメント

## 概要

タイル分割処理において、各ノードが下流からの要求範囲のみを処理するよう最適化する。
現状では ImageEvalNode が画像全体を返却し、フィルタも入力全体を処理しているため、
タイル分割のメモリ効率改善効果が得られていない。

**ステータス**: ✅ **Phase 1-3 完了**

---

## 現状の問題

### 1. ImageEvalNode が画像全体を返却

```cpp
ViewPort ImageEvalNode::evaluate(const RenderRequest& request, ...) {
    (void)request;  // request を無視
    ViewPort result = *imageData;  // 画像全体をコピー
    return result;
}
```

### 2. フィルタオペレーターが入力全体を処理

```cpp
ViewPort BrightnessOperator::applyToSingle(const ViewPort& input,
                                           const RenderRequest& request) const {
    (void)request;  // request を無視
    ViewPort output(working.width, working.height, ...);  // 入力サイズで出力
    // 入力全体を処理
}
```

---

## 設計方針

### 方針1: ImageEvalNode で要求範囲のみを切り出す

ImageEvalNode が request に基づいて必要な範囲のみを SubView として返却する。

```cpp
ViewPort ImageEvalNode::evaluate(const RenderRequest& request, ...) {
    // 要求範囲と画像の交差領域を計算
    // SubView を作成して返却
}
```

**メリット**:
- フィルタオペレーターの変更が不要
- 入力サイズが小さくなるため、自動的にフィルタの処理量が減る

**デメリット**:
- SubView の座標計算が複雑

### 方針2: フィルタオペレーターで request サイズの出力を生成

フィルタオペレーターが request.width/height サイズの出力を生成する。

**メリット**:
- 各オペレーターが独立して最適化可能

**デメリット**:
- 全フィルタの修正が必要
- 座標変換の整合性を保つ必要がある

### 採用方針: 方針1（ImageEvalNode での切り出し）

理由:
1. 変更箇所が ImageEvalNode に集約される
2. フィルタオペレーターは入力サイズで処理するため、自然に最適化される
3. BoxBlur の computeInputRequest による拡大要求が正しく機能する

---

## 実装計画

### Phase 1: ImageEvalNode の修正

**目標**: 要求範囲と画像の交差領域のみを返却

**変更内容**:

```cpp
ViewPort ImageEvalNode::evaluate(const RenderRequest& request,
                                  const RenderContext& context) {
    // 1. 画像の基準相対座標範囲を計算
    //    画像左上: (srcOriginX, srcOriginY) = (-srcOriginX * width, -srcOriginY * height)
    //    画像右下: srcOriginX + width, srcOriginY + height

    // 2. 要求範囲の基準相対座標を計算
    //    要求左上: (-request.originX, -request.originY)
    //    要求右下: request.width - request.originX, request.height - request.originY

    // 3. 交差領域を計算

    // 4. 交差領域がある場合、SubView を作成
    //    - 画像内の開始位置を計算
    //    - SubView の srcOriginX/Y を設定

    // 5. 交差領域がない場合、空の ViewPort を返却
}
```

**座標計算の詳細**:

```
画像: 100x100, 中央基準 (srcOriginX = -50, srcOriginY = -50)
画像の基準相対座標範囲: [-50, 50) x [-50, 50)

要求: width=64, height=64, originX=32, originY=32
要求の基準相対座標範囲: [-32, 32) x [-32, 32)

交差領域（基準相対座標）: [-32, 32) x [-32, 32)
→ 画像内の領域: [18, 82) x [18, 82) （64x64ピクセル）
```

### Phase 2: 動作確認

- タイル分割なしで正常動作を確認
- タイル分割ありで正常動作を確認
- 9点セレクタの各設定で確認
- フィルタノード経由で確認
- アフィン変換ノード経由で確認

### Phase 3: フィルタオペレーターの出力サイズ最適化（オプション）

現状のフィルタオペレーターは入力サイズで出力を生成するが、
request サイズでの出力生成に変更することで、さらなる最適化が可能。

ただし、BoxBlur のように入力範囲を拡大するフィルタでは、
入力 > 出力 となるため、慎重な実装が必要。

---

## 注意事項

### SubView の制約

- SubView は親の ViewPort のメモリを参照するため、親が解放されると無効になる
- imageLibrary の ViewPort は evaluateGraph() 中は有効なので問題なし

### 座標系の整合性

- SubView の srcOriginX/Y は「基準点から見た SubView 左上の相対座標」
- 元画像の srcOriginX/Y とは異なる値になる

### BoxBlur のカーネル半径

- FilterEvalNode::computeInputRequest が既に拡大要求を行っている
- ImageEvalNode が正しく範囲を切り出せば、BoxBlur は自動的に最適化される

---

## 解決済みの問題

### ブラーフィルタとタイル境界 ✅

タイル分割とブラーフィルタを組み合わせた場合、タイルの継ぎ目付近でブラー効果が不自然になる問題。

**原因と解決策**:

1. `RenderRequest::expand()` が `originX/Y` を調整していなかった
   - 修正: `originX + margin, originY + margin` で両側に正しく拡大

2. FilterEvalNode が拡大された入力をそのまま返していた
   - 修正: 処理後に要求サイズに切り出す

**実装**:
- `node_graph.h`: `expand()` で originX/Y も調整
- `evaluation_node.cpp`: FilterEvalNode::evaluate で要求範囲を切り出し

## 既知の問題

### 画像境界でのブラー効果

元画像の境界ではブラーが広がらず、境界がくっきり見える。

**原因**:
- ImageEvalNode は画像範囲外のピクセルを返さない
- 境界付近ではブラーカーネルに必要な周辺ピクセルが不足

**対策案**（TODO に記載）:
- 境界ピクセルの延長（clamp）
- アルファを考慮した境界処理

---

## 関連ファイル

| ファイル | 修正内容 |
|---------|---------|
| `src/fleximg/evaluation_node.cpp` | ImageEvalNode::evaluate, FilterEvalNode::evaluate の修正 |
| `src/fleximg/evaluation_node.h` | FilterEvalNode から kernelRadius 削除 |
| `src/fleximg/node_graph.h` | RenderRequest::expand() の修正 |
| `src/fleximg/operators.h` | NodeOperator に computeInputRequest() 追加 |
| `src/fleximg/operators.cpp` | BoxBlurOperator の最適化 |

---

## 変更履歴

| 日付 | 内容 |
|------|------|
| 2026-01-07 | 初版作成 |
| 2026-01-07 | Phase 1 完了（ImageEvalNode の要求範囲切り出し実装） |
| 2026-01-07 | Phase 2 完了（computeInputRequest を NodeOperator に移動） |
| 2026-01-07 | Phase 3 完了（タイル境界ブラー問題の修正、BoxBlur 最適化） |
