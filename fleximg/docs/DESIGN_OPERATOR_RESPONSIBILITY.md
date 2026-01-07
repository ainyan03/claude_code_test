# オペレーター責務の整理 設計ドキュメント

## 概要

FilterEvalNodeとオペレーターの役割分担を整理し、フィルタ固有の知識をオペレーターに集約する。

**ステータス**: ✅ 実装完了

---

## 現状の問題

### 1. カーネル半径の重複

同じ値が2箇所に存在:

| 場所 | 変数 | 用途 |
|------|------|------|
| FilterEvalNode | `kernelRadius` | computeInputRequest()で拡大量計算 |
| BoxBlurOperator | `radius_` | 実際のブラー処理 |

### 2. フィルタ固有の知識がFilterEvalNodeに漏れている

```cpp
// FilterEvalNode::prepare() 内
if (filterType == "boxblur" && !filterParams.empty()) {
    kernelRadius = static_cast<int>(filterParams[0]);
} else {
    kernelRadius = 0;
}
```

FilterEvalNodeが「boxblurの場合はkernelRadiusが必要」という知識を持っている。
これはオペレーター固有の実装詳細であり、本来オペレーターが担当すべき。

### 3. 拡張性の問題

新しいフィルタを追加する際、フィルタ固有の入力要求があれば FilterEvalNode::prepare() を修正する必要がある。

---

## 設計方針

### NodeOperatorに computeInputRequest() を追加

```cpp
class NodeOperator {
public:
    // 追加: 入力要求を計算（デフォルト: そのまま返す）
    virtual RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const {
        return outputRequest;
    }
    // ... 既存メソッド
};
```

### 各オペレーターでオーバーライド

**BoxBlurOperator**:
```cpp
RenderRequest BoxBlurOperator::computeInputRequest(
    const RenderRequest& outputRequest) const {
    return outputRequest.expand(radius_);
}
```

**他のフィルタ**: デフォルト実装を使用（拡大不要）

### FilterEvalNode の簡略化

```cpp
// 変更前
class FilterEvalNode {
    int kernelRadius;  // 削除

    void prepare(...) {
        op = OperatorFactory::createFilterOperator(...);
        if (filterType == "boxblur" && ...) {  // 削除
            kernelRadius = ...;
        }
    }

    RenderRequest computeInputRequest(...) const {
        return outputRequest.expand(kernelRadius);  // 変更
    }
};

// 変更後
class FilterEvalNode {
    void prepare(...) {
        op = OperatorFactory::createFilterOperator(...);
        // kernelRadius 設定は不要
    }

    RenderRequest computeInputRequest(...) const {
        return op ? op->computeInputRequest(outputRequest) : outputRequest;
    }
};
```

---

## 実装計画

### Phase 1: NodeOperator への追加

1. `operators.h`: NodeOperator に `computeInputRequest()` 仮想メソッド追加
2. デフォルト実装は引数をそのまま返す

### Phase 2: BoxBlurOperator でオーバーライド

1. `operators.h`: BoxBlurOperator に `computeInputRequest()` 宣言追加
2. `operators.cpp`: 実装追加（`radius_` を使用して拡大）

### Phase 3: FilterEvalNode の簡略化

1. `evaluation_node.h`: `kernelRadius` メンバ削除
2. `evaluation_node.cpp`:
   - `prepare()` から `kernelRadius` 設定コードを削除
   - `computeInputRequest()` をオペレーターに委譲

### Phase 4: 動作確認

- ビルド確認
- ブラウザでの動作確認（タイル分割 + ブラーフィルタ）

---

## メリット

1. **単一責任の原則**: フィルタ固有の知識がオペレーターに集約
2. **拡張性向上**: 新しいフィルタ追加時にFilterEvalNodeの修正不要
3. **コード削減**: FilterEvalNodeから`kernelRadius`と条件分岐を削除
4. **保守性向上**: フィルタの入力要求ロジックがフィルタ実装と同じ場所に

---

## 関連ファイル

| ファイル | 変更内容 |
|----------|----------|
| `src/fleximg/operators.h` | NodeOperator に computeInputRequest() 追加 |
| `src/fleximg/operators.cpp` | BoxBlurOperator::computeInputRequest() 実装 |
| `src/fleximg/evaluation_node.h` | FilterEvalNode から kernelRadius 削除 |
| `src/fleximg/evaluation_node.cpp` | prepare(), computeInputRequest() 修正 |

---

## 変更履歴

| 日付 | 内容 |
|------|------|
| 2026-01-07 | 初版作成 |
| 2026-01-07 | 実装完了 |
