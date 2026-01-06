# パイプラインベース評価システム 設計ドキュメント

## 概要

ノードグラフ評価を「パイプライン構築」「描画準備」「タイル描画」の3段階に分離し、ポインタベースの直接接続により効率的な処理を実現するアーキテクチャ。

**ステータス**: ✅ **実装完了・本番稼働中**

本システムは `NodeGraphEvaluator` の唯一の評価エンジンとして稼働しています。
旧実装（文字列ベース評価）は削除済みです。

**設計目標**:
1. ノード検索のオーバーヘッド削減（O(n) → O(1)）
2. タイルごとのキャッシュ再構築を排除
3. 要求伝播と評価の一体化によるコード簡潔化
4. 組込み環境での効率的なメモリ使用

## 旧実装の問題点（参考）

### 問題1: ノード検索のオーバーヘッド

```cpp
// 現在: 文字列比較によるO(n)検索が頻発
for (const auto& n : nodes) {
    if (n.id == nodeId) { ... }  // タイルごと、ノードごとに実行
}
```

### 問題2: タイルごとのキャッシュ再構築

```cpp
void propagateRequests(const RenderRequest& tileRequest) {
    nodeRequestCache.clear();  // 毎タイルでクリア
    // ... 再構築
}
```

### 問題3: 2パス処理の冗長性

```cpp
// パス1: 要求を全ノードにキャッシュ
propagateRequests(tile);

// パス2: キャッシュを参照して評価
evaluateTile(tile);
```

## 解決アプローチ

### 3段階処理アーキテクチャ

```
【段階1: パイプライン構築】（ノード構成変更時のみ）
setNodes() / setConnections() 呼び出し後、初回 evaluateGraph() で実行
・GraphNode/GraphConnection → EvaluationNode のポインタグラフに変換
・NodeOperator インスタンスを事前生成

【段階2: 描画準備】（evaluateGraph 開始時に1回）
・逆行列計算（アフィン変換用）
・カーネル準備（フィルタ用）
・画像サイズ取得

【段階3: タイル描画】（タイル分割ループ）
・出力ノードから再帰的に evaluate() を呼び出し
・要求伝播と結果返却が一体化した単一パスで処理
```

### 処理フロー図

```
【パイプライン構築】
GraphNode[]           EvaluationNode グラフ
     │                      │
     ▼                      ▼
┌─────────┐  buildPipeline()  ┌─────────┐
│ "image" │ ───────────────→ │ ImageNode│◀─┐
└─────────┘                   └─────────┘  │ inputs[]
┌─────────┐                   ┌─────────┐  │
│"affine" │ ───────────────→ │AffineNode│──┘
└─────────┘                   └─────────┘◀─┐
┌─────────┐                   ┌─────────┐  │
│"output" │ ───────────────→ │OutputNode│──┘
└─────────┘                   └─────────┘

【タイル描画】
OutputNode.evaluate(tileRequest)
    │
    │ computeInputRequest()
    ▼
AffineNode.evaluate(affineRequest)
    │
    │ computeInputRequest()
    ▼
ImageNode.evaluate(imageRequest)
    │
    │ return ViewPort (画像データ)
    ▼
AffineNode: op->apply() で変換
    │
    │ return ViewPort (変換結果)
    ▼
OutputNode: 最終結果として返却
```

## データ構造

### EvaluationNode（評価ノード基底クラス）

```cpp
class EvaluationNode {
public:
    virtual ~EvaluationNode() = default;

    // メイン評価メソッド（派生クラスで実装）
    virtual ViewPort evaluate(const RenderRequest& request,
                              const RenderContext& context) = 0;

    // 入力要求の計算（派生クラスで実装）
    virtual RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const = 0;

    // ノード情報
    std::string id;                          // デバッグ用ID
    std::vector<EvaluationNode*> inputs;     // 上流ノードへのポインタ

protected:
    // 準備済みデータ（派生クラスで使用）
    bool prepared = false;
};
```

### ImageEvalNode（画像ノード）

```cpp
class ImageEvalNode : public EvaluationNode {
public:
    ViewPort evaluate(const RenderRequest& request,
                      const RenderContext& context) override {
        // 画像データを返却（終端ノード）
        ViewPort result = *imageData;
        result.srcOriginX = srcOriginX * result.width;
        result.srcOriginY = srcOriginY * result.height;
        return result;
    }

    RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const override {
        // 画像ノードは入力がないので空を返す
        return RenderRequest{};
    }

    // 画像データへの参照
    const ViewPort* imageData = nullptr;
    double srcOriginX = 0.5;
    double srcOriginY = 0.5;
};
```

### FilterEvalNode（フィルタノード）

```cpp
class FilterEvalNode : public EvaluationNode {
public:
    ViewPort evaluate(const RenderRequest& request,
                      const RenderContext& context) override {
        // 1. 入力要求を計算
        RenderRequest inputReq = computeInputRequest(request);

        // 2. 上流ノードを評価
        ViewPort input = inputs[0]->evaluate(inputReq, context);

        // 3. フィルタ処理を適用
        OperatorContext ctx(context.totalWidth, context.totalHeight,
                           request.originX, request.originY);
        ViewPort result = op->apply({input}, ctx);
        result.srcOriginX = input.srcOriginX;
        result.srcOriginY = input.srcOriginY;
        return result;
    }

    RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const override {
        // カーネル半径分だけ領域を拡大
        return outputRequest.expand(kernelRadius);
    }

    std::unique_ptr<NodeOperator> op;
    int kernelRadius = 0;
};
```

### AffineEvalNode（アフィン変換ノード）

```cpp
class AffineEvalNode : public EvaluationNode {
public:
    ViewPort evaluate(const RenderRequest& request,
                      const RenderContext& context) override {
        // 1. 上流ノードを評価
        RenderRequest inputReq = computeInputRequest(request);
        ViewPort input = inputs[0]->evaluate(inputReq, context);

        // 2. フォーマット変換
        if (input.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
            input = input.convertTo(PixelFormatIDs::RGBA16_Premultiplied);
        }

        // 3. アフィン変換を適用
        double inputOriginX = input.srcOriginX;
        double inputOriginY = input.srcOriginY;
        double baseOffset = std::max(input.width, input.height);
        double outputOffsetX = baseOffset + std::abs(matrix.tx);
        double outputOffsetY = baseOffset + std::abs(matrix.ty);

        auto affineOp = OperatorFactory::createAffineOperator(
            matrix, inputOriginX, inputOriginY,
            outputOffsetX, outputOffsetY,
            input.width + outputOffsetX * 2,
            input.height + outputOffsetY * 2);

        OperatorContext ctx(context.totalWidth, context.totalHeight,
                           request.originX, request.originY);
        ViewPort result = affineOp->apply({input}, ctx);

        result.srcOriginX = inputOriginX + outputOffsetX;
        result.srcOriginY = inputOriginY + outputOffsetY;
        return result;
    }

    RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const override {
        // 逆行列で出力要求を入力座標に変換
        // （事前計算済みの逆行列を使用）
        // ... AABB計算
        return inputAABB;
    }

    AffineMatrix matrix;
    // 事前計算済み逆行列
    int32_t fixedInvA, fixedInvB, fixedInvC, fixedInvD;
    int32_t fixedInvTx, fixedInvTy;
};
```

### CompositeEvalNode（合成ノード）

```cpp
class CompositeEvalNode : public EvaluationNode {
public:
    ViewPort evaluate(const RenderRequest& request,
                      const RenderContext& context) override {
        // 1. 全入力ノードを評価
        std::vector<ViewPort> inputImages;
        for (size_t i = 0; i < inputs.size(); i++) {
            ViewPort img = inputs[i]->evaluate(request, context);

            // フォーマット変換
            if (img.formatID != PixelFormatIDs::RGBA16_Premultiplied) {
                img = img.convertTo(PixelFormatIDs::RGBA16_Premultiplied);
            }

            // アルファ適用
            if (alphas[i] != 1.0) {
                // ... アルファ乗算
            }

            inputImages.push_back(std::move(img));
        }

        // 2. 合成処理
        OperatorContext ctx(context.totalWidth, context.totalHeight,
                           request.originX, request.originY);
        return compositeOp->apply(inputImages, ctx);
    }

    RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const override {
        // 合成ノードは入力要求をそのまま伝播
        return outputRequest;
    }

    std::unique_ptr<CompositeOperator> compositeOp;
    std::vector<double> alphas;  // 各入力のアルファ値
};
```

### PipelineBuilder（パイプライン構築）

```cpp
class PipelineBuilder {
public:
    // GraphNode/GraphConnection からパイプラインを構築
    static std::unique_ptr<EvaluationNode> build(
        const std::vector<GraphNode>& nodes,
        const std::vector<GraphConnection>& connections,
        const std::map<int, ViewPort>& imageLibrary
    ) {
        // 1. ノードIDからEvaluationNodeへのマップを構築
        std::map<std::string, std::unique_ptr<EvaluationNode>> nodeMap;

        for (const auto& node : nodes) {
            nodeMap[node.id] = createEvalNode(node, imageLibrary);
        }

        // 2. コネクションに基づいてポインタを接続
        for (const auto& conn : connections) {
            auto* fromNode = nodeMap[conn.fromNodeId].get();
            auto* toNode = nodeMap[conn.toNodeId].get();
            toNode->inputs.push_back(fromNode);
        }

        // 3. 出力ノードを検索して返却
        for (auto& [id, node] : nodeMap) {
            if (/* node is output type */) {
                return std::move(node);
            }
        }
        return nullptr;
    }

private:
    static std::unique_ptr<EvaluationNode> createEvalNode(
        const GraphNode& node,
        const std::map<int, ViewPort>& imageLibrary
    ) {
        if (node.type == "image") {
            auto evalNode = std::make_unique<ImageEvalNode>();
            evalNode->imageData = &imageLibrary.at(node.imageId);
            evalNode->srcOriginX = node.srcOriginX;
            evalNode->srcOriginY = node.srcOriginY;
            return evalNode;
        }
        else if (node.type == "filter") {
            auto evalNode = std::make_unique<FilterEvalNode>();
            evalNode->op = OperatorFactory::createFilterOperator(
                node.filterType, node.filterParams);
            return evalNode;
        }
        // ... 他のノードタイプ
    }
};
```

## NodeGraphEvaluator の改修

```cpp
class NodeGraphEvaluator {
public:
    // ... 既存のメソッド ...

    Image evaluateGraph() {
        // パイプライン構築（必要な場合のみ）
        if (pipelineDirty) {
            pipeline = PipelineBuilder::build(nodes, connections, imageLibrary);
            pipelineDirty = false;
        }

        if (!pipeline) {
            return Image(canvasWidth, canvasHeight);
        }

        // 描画準備
        RenderContext context{
            canvasWidth, canvasHeight,
            dstOriginX, dstOriginY,
            tileStrategy,
            customTileWidth, customTileHeight
        };

        // タイル分割なしの場合
        if (tileStrategy == TileStrategy::None) {
            RenderRequest fullRequest{
                0, 0, canvasWidth, canvasHeight,
                dstOriginX, dstOriginY
            };
            ViewPort result = pipeline->evaluate(fullRequest, context);
            return finalizeOutput(result);
        }

        // タイル分割処理
        Image result(canvasWidth, canvasHeight);
        for (int ty = 0; ty < context.getTileCountY(); ty++) {
            for (int tx = 0; tx < context.getTileCountX(); tx++) {
                RenderRequest tileReq = RenderRequest::fromTile(context, tx, ty);
                ViewPort tileResult = pipeline->evaluate(tileReq, context);
                copyTileToImage(tileResult, result, tileReq);
            }
        }
        return result;
    }

    void setNodes(const std::vector<GraphNode>& newNodes) {
        nodes = newNodes;
        pipelineDirty = true;  // パイプライン再構築をマーク
    }

    void setConnections(const std::vector<GraphConnection>& newConnections) {
        connections = newConnections;
        pipelineDirty = true;  // パイプライン再構築をマーク
    }

private:
    std::unique_ptr<EvaluationNode> pipeline;
    bool pipelineDirty = true;

    ViewPort finalizeOutput(ViewPort& result) {
        // srcOrigin と dstOrigin の差を解消
        // ... 既存の最終合成処理
    }
};
```

## 実装状況

### Phase 1: 基盤整備 ✅
- [x] `EvaluationNode` 基底クラスの定義
- [x] `evaluation_node.h` ファイル作成
- [x] 各派生クラスのインターフェース定義

### Phase 2: 派生クラス実装 ✅
- [x] `ImageEvalNode` 実装
- [x] `FilterEvalNode` 実装
- [x] `AffineEvalNode` 実装
- [x] `CompositeEvalNode` 実装
- [x] `OutputEvalNode` 実装

### Phase 3: パイプライン構築 ✅
- [x] `PipelineBuilder` 実装
- [x] ノード接続のポインタ化
- [x] 出力ノード検索
- [x] `Pipeline` 構造体による所有権管理

### Phase 4: NodeGraphEvaluator 統合 ✅
- [x] `evaluateGraph()` を新アーキテクチャに移行
- [x] `pipelineDirty` フラグによる再構築制御

### Phase 5: テスト・最適化 ✅
- [x] 既存動作との互換性確認
- [ ] パフォーマンス計測（将来）
- [ ] メモリ使用量確認（将来）

### Phase 6: 旧実装削除 ✅
- [x] 文字列ベース評価コードを削除（約680行削減）
- [x] 旧キャッシュ変数を削除
- [x] 旧ヘルパー関数を削除

## 期待される効果

| 指標 | 現在 | 改善後 |
|------|------|--------|
| ノード検索 | O(n) × タイル数 × ノード数 | O(1) |
| キャッシュ再構築 | タイルごと | なし |
| コード量 | propagate + evaluate | evaluate のみ |
| 関数呼び出し | 文字列ID経由 | ポインタ直接 |

## 注意点

### メモリ管理
- `EvaluationNode` は `std::unique_ptr` で管理
- パイプライン全体は出力ノードから所有権を辿れる構造
- `imageLibrary` の画像データはポインタ参照（コピーなし）

### スレッドセーフティ
- 現時点ではシングルスレッド前提
- 将来的にタイル並列処理を行う場合は `evaluate()` の結果をスレッドローカルにする必要あり

### 循環参照
- グラフ構築時に循環検出を行い、エラーとする
- ポインタ接続では循環があると無限ループになるため必須

## 関連ドキュメント

- [DESIGN_NODE_OPERATOR.md](DESIGN_NODE_OPERATOR.md): ノードオペレーター統一設計

## 変更履歴

| 日付 | 内容 |
|------|------|
| 2025-01-06 | 初版作成 |
| 2026-01-06 | 実装完了、旧実装削除（約680行削減） |
