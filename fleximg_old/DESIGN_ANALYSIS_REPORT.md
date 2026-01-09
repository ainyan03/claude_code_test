# fleximg 設計分析レポート

作成日: 2026-01-09

## 全体評価

全体として**高品質で洗練された設計**です。責務分離、命名規則、型安全性において優れた判断がなされています。以下に設計上の疑問点・改善案を報告します。

---

## 1. 構造上の疑問点

### 1.1 `ImageBuffer` が `ViewPort` を継承している点

**現状**:
```cpp
struct ImageBuffer : public ViewPort { ... }
```

**疑問**:
継承により「ImageBufferはViewPortとして振る舞える」が、意図しないスライシングのリスクがあります。

```cpp
void process(ViewPort v) { ... }  // 値渡し → ImageBufferが渡されるとスライス
```

**見解**:
ドキュメントに注意書きがありますが、コンポジション（has-a）の方が安全です：
```cpp
struct ImageBuffer {
    ViewPort view_;   // メンバとして保持
    ViewPort view() const { return view_; }
};
```
ただし現在の継承方式も、`view()`メソッドで明示的にViewPortを取得する運用が徹底されていれば問題ありません。

---

### 1.2 `GraphNode` の設計 - 多目的構造体

**現状**:
```cpp
struct GraphNode {
    std::string type;  // "image", "filter", "composite", "affine", "output"
    int imageId;           // image/output用
    std::string filterType;  // filter用
    AffineMatrix affineMatrix; // affine用
    std::vector<CompositeInput> compositeInputs;  // composite用
};
```

**疑問**:
全ノードタイプの属性を1つの構造体に詰め込んでいます。例えばaffineノードなのに`filterType`フィールドが存在します。

**見解**:
これは**意図的な設計選択**と思われます：
- JS→WASM間のデータ転送でvariantやポリモーフィズムは複雑になる
- 単純なstructの配列は扱いやすい
- Emscriptenバインディングとの相性が良い

改善するなら`std::variant`や継承ですが、現状でも動作上問題なく、過剰設計になりかねません。

---

### 1.3 `operators.h` → `node_graph.h` の循環依存

**現状**:
```cpp
// operators.h
#include "node_graph.h"  // RenderRequestのために
```

```cpp
// node_graph.h は operators.h を include していない
```

**疑問**:
`RenderRequest`という小さな構造体のために`operators.h`が`node_graph.h`全体をインクルードしています。

**見解**:
`RenderRequest`と`RenderContext`を別ファイル（例: `render_types.h`）に分離すれば：
- 依存関係がクリーンに
- コンパイル時間短縮（特に大規模プロジェクトで）

```cpp
// render_types.h
struct RenderRequest { ... };
struct RenderContext { ... };
```

---

### 1.4 ノードタイプの文字列比較

**現状**:
```cpp
if (node.type == "image") { ... }
else if (node.type == "filter") { ... }
// PipelineBuilder::createEvalNode内
```

**疑問**:
文字列比較はコンパイル時にエラーを検出できません。`"imge"`（typo）でもコンパイルは通ります。

**見解**:
enumの使用が型安全です：
```cpp
enum class NodeType { Image, Filter, Affine, Composite, Output };
```
ただしJS/WASMバインディングでは文字列の方が扱いやすいため、これも**意図的なトレードオフ**かもしれません。

---

## 2. 「なぜこうなっているのか？」と思う箇所

### 2.1 `EvaluationNode::inputs` が `vector<EvaluationNode*>` な理由

**現状**:
```cpp
std::vector<EvaluationNode*> inputs;  // 生ポインタ
```

**疑問**:
所有権は`Pipeline::nodes`が持っているとはいえ、生ポインタは意図が不明瞭です。

**推測**:
- `weak_ptr`は循環参照用であり、ここでは不要
- `non-owning reference`を表現する標準的な方法が生ポインタ
- パフォーマンス上も最適

**改善案**（ドキュメント追加）:
```cpp
// 注: Pipelineが全ノードを所有するため、生ポインタで参照
// ライフタイム保証: Pipeline破棄までは有効
std::vector<EvaluationNode*> inputs;
```

---

### 2.2 `FilterEvalNode::prepare()` での遅延初期化

**現状**:
```cpp
void FilterEvalNode::prepare(const RenderContext& context) {
    op = OperatorFactory::createFilterOperator(filterType, filterParams);
}
```

**疑問**:
毎フレーム呼ばれる可能性がある`prepare()`でオペレーターを再生成していますが、パラメータが変わらないなら無駄では？

**確認**:
`evaluateWithPipeline`で毎回`prepare()`が呼ばれています：
```cpp
void NodeGraphEvaluator::evaluateWithPipeline(const RenderContext& context) {
    buildPipelineIfNeeded();
    pipeline_->prepare(context);  // 毎回呼ばれる
    // ...
}
```

**見解**:
`prepared_`フラグはありますが使われていない？確認が必要です。

---

### 2.3 OutputEvalNode の「ゼロクリア→コピー」パターン

**現状**:
```cpp
// 出力先の該当タイル範囲をゼロクリア
std::memset(dstRow, 0, clearWidth * bytesPerPixel);

// ... 上流ノードを評価 ...

// 入力データを出力先にコピー
std::memcpy(dstRow, srcRow, copyWidth * bytesPerPixel);
```

**疑問**:
入力が完全にカバーする場合、ゼロクリアは無駄では？

**見解**:
ロジックの単純化を優先した設計と思われます。最適化するなら：
```cpp
// 入力が完全カバーならゼロクリアスキップ
if (!inputCoversFullTile) {
    std::memset(...);
}
```

---

## 3. 改善提案

### 3.1 高優先度

| 提案 | 理由 | 影響範囲 |
|------|------|---------|
| `RenderRequest`/`RenderContext`を別ファイルに | 依存関係整理 | 小 |
| `prepared_`フラグの活用確認 | 無駄な再初期化防止 | 小 |

### 3.2 中優先度

| 提案 | 理由 | 影響範囲 |
|------|------|---------|
| ノードタイプのenum化 | 型安全性 | 中（JSバインディング変更必要） |
| GraphNodeのバリデーション追加 | 不正データ検出 | 小 |

### 3.3 低優先度（将来検討）

| 提案 | 理由 | 影響範囲 |
|------|------|---------|
| ImageBuffer→ViewPort継承をコンポジションに | スライシング防止 | 大 |
| GraphNode のunion/variant化 | メモリ効率 | 大 |

---

## 4. 良い設計と思う点

分析中に見つけた優れた設計判断：

1. **基準相対座標系の統一** - タイル分割時も座標系が一貫
2. **EvalResultのムーブオンリー** - 不要なコピー防止
3. **RAII原則の徹底** - ImageBufferのメモリ管理
4. **条件付きコンパイル** - `FLEXIMG_DEBUG_PERF_METRICS`でリリース時オーバーヘッドゼロ
5. **OperatorFactory パターン** - オペレーター生成の一元化
6. **明確な責務分離** - ViewPort（ビュー）/ ImageBuffer（所有）/ EvalResult（結果）
7. **一貫した命名規則** - クラス名、メソッド名、定数がすべて統一されたスタイル

---

## 5. 関連ファイル一覧

| ファイル | 内容 |
|---------|------|
| `src/fleximg/node_graph.h` | NodeGraphEvaluator, RenderRequest, RenderContext |
| `src/fleximg/node_graph.cpp` | パイプライン評価ループ |
| `src/fleximg/operators.h` | NodeOperator階層、フィルタ/合成オペレーター |
| `src/fleximg/operators.cpp` | オペレーター実装 |
| `src/fleximg/evaluation_node.h` | EvaluationNode階層、Pipeline |
| `src/fleximg/evaluation_node.cpp` | 各ノードのevaluate()実装 |
| `src/fleximg/viewport.h` | ViewPort（純粋ビュー） |
| `src/fleximg/image_buffer.h` | ImageBuffer（メモリ所有） |
| `src/fleximg/eval_result.h` | EvalResult（パイプライン結果） |
| `src/fleximg/pixel_format.h` | ピクセルフォーマット定義 |
