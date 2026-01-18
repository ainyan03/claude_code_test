# アルファチャンネル分離・結合ノード（Alpha Merge/Split Nodes）

## 概要

RGB画像とアルファチャンネルを独立して処理できるよう、チャンネルの分離と結合を行う専用ノード。

- **AlphaMergeNode**: RGB画像 + Alpha画像 → RGBA画像（2入力→1出力）
- **AlphaSplitNode**: RGBA画像 → RGB画像 + Alpha画像（1入力→2出力）

## 動機

### ユースケース

1. **マスク合成ワークフロー**
   - 写真素材（RGB）と別途作成したマスク画像（Alpha）を結合して透過画像を生成
   - デザインツールやペイントソフトで個別に作成した素材の合成

2. **チャンネル別処理パイプライン**
   ```
   RGBA画像 → Split → RGB: カラー補正
                    └ Alpha: ブラー/エッジ処理
                         ↓
                       Merge → 処理済みRGBA
   ```
   - アルファだけ別経路でブラーやエッジ検出を適用
   - RGB/Alphaを独立したパラメータで調整してから再結合

3. **異なるソースの合成**
   - カメラ映像（RGB）+ 深度センサーデータ（Alpha値として使用）
   - リアルタイム生成コンテンツとマスク画像の動的合成

### 現状の制約

既存の`AlphaNode`はアルファ値のスケール調整のみで、チャンネルの分離や外部アルファの注入はできない。また、`CompositeNode`は既存のアルファ値を使ったブレンド合成のみを行う。

## 提案

### AlphaMergeNode（2入力→1出力）

#### インターフェース

```cpp
class AlphaMergeNode : public Node {
public:
    AlphaMergeNode();

    // 入力ポート:
    // - ポート0: RGB入力（RGBAの場合Aは無視）
    // - ポート1: Alpha入力（グレースケールまたはRGBAのRチャンネル使用）

    // Node interface
    void pullPrepare(const RenderContext& ctx) override;
    RenderResult pullProcess(const RenderRequest& request) override;
    void pullFinalize() override;
};
```

#### 処理フロー

```
1. 両入力の共通領域（intersection）を計算
2. RGB入力からピクセルデータ取得（formatはRGBA8/RGBA16等に変換）
3. Alpha入力からピクセルデータ取得（グレースケールまたはRチャンネル）
4. ラインフィルタでピクセル単位でアルファ値を置換
   - RGB_in[i].a = Alpha_in[i].gray（または Alpha_in[i].r）
5. 結果を出力
```

#### 入力フォーマットの扱い

| RGB入力 | Alpha入力 | 処理 |
|---------|-----------|------|
| RGB565 | Gray8 | RGB→RGBA8変換後、Gray値をA値に設定 |
| RGBA8 | Gray8 | 既存A値を破棄、Gray値をA値に設定 |
| RGBA16 | RGBA8 | RGBA8のR値をRGBA16のA値に設定（スケール変換） |

### AlphaSplitNode（1入力→2出力）

#### インターフェース

```cpp
class AlphaSplitNode : public Node {
public:
    AlphaSplitNode();

    // 入力ポート:
    // - ポート0: RGBA入力

    // 出力ポート:
    // - ポート0: RGB出力（アルファ除去、不透明化）
    // - ポート1: Alpha出力（グレースケール画像）
};
```

#### 処理フロー

```
1. 入力からRGBA画像を取得
2. 出力0: RGB画像生成
   - アルファ値を1.0（不透明）に設定
   - または Premultiplied形式の場合は正規化
3. 出力1: Alpha画像生成
   - A値をR=G=B=Aのグレースケールに変換
   - またはGray8フォーマットで出力
```

### 実装の詳細

#### ノードタイプ定義

`perf_metrics.h` に追加:

```cpp
namespace NodeType {
    // ... 既存 ...
    constexpr int AlphaMerge = 13;
    constexpr int AlphaSplit = 14;
    constexpr int Count = 15;
}
```

#### 基底クラス

- `Node` から直接継承（`FilterNodeBase`は1入力前提のため不適）
- `CompositeNode`と`DistributorNode`を参考に実装

#### サイズ不一致の処理

**AlphaMergeNode**:
- 両入力の共通領域（intersection）のみ処理
- はみ出た部分は未定義（または透明）
- リサイズが必要な場合は前段に明示的なAffineNodeを要求

**AlphaSplitNode**:
- 入力と同サイズの2出力を生成

#### パフォーマンス最適化

- ラインフィルタ方式でスキャンライン処理
- メモリコピー最小化（可能な場合は参照モード）
- フォーマット変換は最小限に抑える

## 考慮事項

### 1. フォーマット変換のコスト

- RGB565 → RGBA8 → AlphaMerge のような多段変換は非効率
- 将来的には「フォーマット交渉」（IDEA_FORMAT_NEGOTIATION.md）で最適化可能

### 2. Premultiplied vs Straight

- AlphaMergeの出力: Straight形式が自然（アルファを後付けするため）
- AlphaSplitの入力: Premultiplied形式の場合、RGB値の正規化が必要
- ユーザーに選択肢を提供する？または自動判定？

### 3. エラーハンドリング

- 入力未接続時の動作
  - AlphaMerge: RGB未接続 → 黒、Alpha未接続 → 不透明
  - AlphaSplit: 入力未接続 → エラー
- サイズゼロの入力への対応

### 4. 命名

候補:
- `AlphaMergeNode` / `AlphaSplitNode` （推奨）
- `AlphaCombineNode` / `AlphaSeparateNode`
- `ChannelMergeNode` / `ChannelSplitNode` （将来の拡張性）

### 5. 将来の拡張可能性

より汎用的な「チャンネル操作ノード」への発展:
```
ChannelMergeNode (4入力 → 1出力)
  入力0: R, 入力1: G, 入力2: B, 入力3: A
  → RGBA合成

ChannelSplitNode (1入力 → 4出力)
  RGBA → R, G, B, A個別出力
```

ただし、ユースケースが限定的なため、まずはAlpha専用で実装し、需要を見て判断。

## 実装順序

### Phase 1: AlphaMergeNode

1. ノードタイプ定義追加
2. 基本クラス実装（2入力、1出力）
3. プル型パイプライン実装
4. ラインフィルタ処理実装
5. テストケース作成

### Phase 2: AlphaSplitNode

1. ノードタイプ定義追加
2. 基本クラス実装（1入力、2出力）
3. プル型パイプライン実装（DistributorNode参考）
4. ラインフィルタ処理実装
5. テストケース作成

### Phase 3: WebUIバインディング

1. JavaScriptバインディング追加
2. UIコンポーネント作成
3. サンプルシーン追加

## 関連

- **CompositeNode**: 複数入力ノードの実装参考
- **DistributorNode**: 複数出力ノードの実装参考
- **AlphaNode**: 既存のアルファ処理ノード
- **IDEA_FORMAT_NEGOTIATION.md**: フォーマット変換最適化の将来的な方向性

## 参考実装

### 既存のチャンネル操作

```cpp
// filters.h より
void alpha_line(uint8_t* pixels, int count, const LineFilterParams& params);
// params.value1: アルファスケール（0.0〜1.0）

void grayscale_line(uint8_t* pixels, int count, const LineFilterParams& params);
// RGB → グレースケール変換（チャンネル統合の例）
```

### CompositeNodeの複数入力処理

```cpp
// composite_node.cpp より
void CompositeNode::pullPrepare(const RenderContext& ctx) {
    for (int i = 0; i < numInputs; i++) {
        Node* upstream = upstreamNode(i);
        if (upstream) {
            upstream->pullPrepare(ctx);
        }
    }
}

RenderResult CompositeNode::pullProcess(const RenderRequest& request) {
    for (int i = 0; i < numInputs; i++) {
        Node* upstream = upstreamNode(i);
        RenderResult inputResult = upstream->pullProcess(request);
        // canvas_utils::placeOnto() で合成
    }
    return result;
}
```
