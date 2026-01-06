# 2パス評価システム 設計ドキュメント

## 概要

ノードグラフ評価を「事前準備」「要求伝播」「実評価」の段階に分離し、各ノードが必要最小限の領域のみを処理するアーキテクチャ。

**設計目標**:
1. 画像切れ問題の解決
2. メモリ効率の向上
3. **組込み環境への移植性**（タイル分割処理対応）

## 現状の問題

### 問題1: 画像切れ

```
キャンバス: 200x200
入力画像: 128x128
アフィン変換のoutputOffset: 128

→ 画像は (128, 128) から描画開始
→ 必要な出力領域: 256x256
→ 実際の出力ViewPort: 200x200（canvasSize）
→ 右下56ピクセル分が切れる
```

### 問題2: メモリ非効率

```
現状: 中間バッファは常にcanvasSize
→ 200x200出力に10x10画像を配置する場合も200x200確保
→ 無駄なメモリ使用
```

## 解決アプローチ

### 3段階評価（タイル分割対応）

組込み環境でのメモリ制約を考慮し、出力領域を分割して繰り返し処理できる構造とする。

```
【段階0: 事前準備（1回のみ）】
出力全体サイズを各ノードに通知 → 事前計算パラメータを準備
output → composite → affine → filter → image
(200x200)             逆行列    カーネル  画像サイズ
                      算出      準備      取得

【段階1: 要求伝播（タイルごと）】
部分矩形の要求を伝播 → 必要入力領域を算出
output → composite → affine → filter → image
(0,0-64,64)          逆変換    拡大     交差計算
                     (事前計算済み行列使用)

【段階2: 実評価（タイルごと）】
image → filter → affine → composite → output
  ↓        ↓        ↓          ↓         ↓
タイル分の領域のみ処理 → 省メモリ
```

### 分割戦略

| 戦略 | 説明 | 用途 |
|------|------|------|
| 全体一括 | 分割なし（現状と同等） | PC環境、高速処理優先 |
| スキャンライン | 1行ずつ処理 | 極小メモリ環境 |
| タイル (64x64等) | 小矩形単位で処理 | 組込み環境の標準 |
| 適応的 | メモリ残量に応じて動的決定 | 汎用 |

### メリット

1. **事前計算の再利用**: 逆行列等はタイルごとに再計算不要
2. **メモリ上限の制御**: 最大バッファサイズを制限可能
3. **進捗表示**: タイル単位で進捗を報告可能
4. **中断・再開**: タイル境界で処理を中断可能

## データ構造

### RenderContext（出力全体情報 - 段階0で伝播）

```cpp
struct RenderContext {
    // 出力全体のサイズ
    int totalWidth, totalHeight;

    // 出力基準座標（dstOrigin）
    double originX, originY;

    // 分割戦略
    enum class TileStrategy {
        None,       // 分割なし
        Scanline,   // 1行ずつ
        Tile64,     // 64x64タイル
        Custom      // カスタムサイズ
    };
    TileStrategy strategy = TileStrategy::None;
    int tileWidth = 0, tileHeight = 0;  // Custom用

    // タイル数を取得
    int getTileCountX() const;
    int getTileCountY() const;
    RenderRequest getTileRequest(int tileX, int tileY) const;
};
```

### RenderRequest（部分矩形要求 - 段階1で伝播）

```cpp
struct RenderRequest {
    // 必要な出力領域（ローカル座標系）
    int x, y;           // 左上座標
    int width, height;  // サイズ

    // 基準座標（この点を中心に配置）
    double originX, originY;

    // ユーティリティ
    bool isEmpty() const { return width <= 0 || height <= 0; }
    RenderRequest intersect(const RenderRequest& other) const;
    RenderRequest expand(int margin) const;  // フィルタ用
};
```

### NodePreparedData（事前計算データ - 段階0で生成）

```cpp
// 各ノードタイプ固有の事前計算データ
struct AffinePreparedData {
    // 逆行列（固定小数点）
    int32_t fixedInvA, fixedInvB, fixedInvC, fixedInvD;
    int32_t fixedInvTx, fixedInvTy;

    // 原点オフセット適用済み
    bool prepared = false;
};

struct FilterPreparedData {
    // カーネルデータ（BoxBlur等）
    std::vector<float> kernel;
    int kernelRadius = 0;

    bool prepared = false;
};

// ノードごとの事前計算データを保持
std::map<std::string, std::variant<AffinePreparedData, FilterPreparedData>> nodePreparedCache;
```

### ノード要求キャッシュ

```cpp
// NodeGraphEvaluator に追加
std::map<std::string, RenderRequest> nodeRequestCache;
```

## 各ノードタイプの要求計算

### 出力ノード (output)

出力ノードは評価の起点。キャンバス全体を要求。

```cpp
RenderRequest outputRequest = {
    .x = 0, .y = 0,
    .width = canvasWidth, .height = canvasHeight,
    .originX = dstOriginX, .originY = dstOriginY
};
```

### 合成ノード (composite)

出力要求をそのまま各入力に伝播。

```cpp
RenderRequest computeInputRequest(const RenderRequest& outputReq) {
    // 各入力に同じ要求を伝播
    return outputReq;
}
```

### アフィン変換ノード (affine)

出力要求領域の4頂点を逆変換し、AABB（軸並行境界ボックス）を算出。

```cpp
RenderRequest computeInputRequest(const RenderRequest& outputReq) {
    // 出力領域の4頂点
    Point corners[4] = {
        {outputReq.x, outputReq.y},
        {outputReq.x + outputReq.width, outputReq.y},
        {outputReq.x, outputReq.y + outputReq.height},
        {outputReq.x + outputReq.width, outputReq.y + outputReq.height}
    };

    // 逆行列を適用（原点中心の変換を考慮）
    AffineMatrix invMatrix = matrix.inverse();

    // AABBを算出
    double minX = INF, minY = INF, maxX = -INF, maxY = -INF;
    for (auto& corner : corners) {
        // 基準座標からの相対位置に変換
        double relX = corner.x - outputReq.originX;
        double relY = corner.y - outputReq.originY;

        // 逆変換
        double srcX = invMatrix.a * relX + invMatrix.b * relY + srcOriginX;
        double srcY = invMatrix.c * relX + invMatrix.d * relY + srcOriginY;

        minX = std::min(minX, srcX);
        minY = std::min(minY, srcY);
        maxX = std::max(maxX, srcX);
        maxY = std::max(maxY, srcY);
    }

    return {
        .x = (int)std::floor(minX),
        .y = (int)std::floor(minY),
        .width = (int)std::ceil(maxX) - (int)std::floor(minX),
        .height = (int)std::ceil(maxY) - (int)std::floor(minY),
        .originX = srcOriginX,
        .originY = srcOriginY
    };
}
```

### フィルタノード (filter)

フィルタの種類に応じて領域を拡大。

```cpp
RenderRequest computeInputRequest(const RenderRequest& outputReq) {
    int margin = 0;

    if (filterType == "boxblur") {
        // BoxBlur: カーネル半径分の余白が必要
        int radius = (int)filterParams[0];
        margin = radius;
    }
    // その他のフィルタは margin = 0（領域変更なし）

    return outputReq.expand(margin);
}
```

### 画像ノード (image)

入力要求と画像の実サイズの交差領域を算出。

```cpp
RenderRequest computeInputRequest(const RenderRequest& outputReq) {
    // 画像の実領域
    RenderRequest imageRect = {
        .x = 0, .y = 0,
        .width = imageWidth, .height = imageHeight,
        .originX = srcOriginX * imageWidth,
        .originY = srcOriginY * imageHeight
    };

    // 要求との交差
    return outputReq.intersect(imageRect);
}
```

## 実装手順

### Phase 1: 基盤整備

1. **RenderContext / RenderRequest 構造体の追加** (`node_graph.h`)
   ```cpp
   struct RenderContext {
       int totalWidth = 0, totalHeight = 0;
       double originX = 0, originY = 0;
       // ... タイル戦略
   };

   struct RenderRequest {
       int x = 0, y = 0;
       int width = 0, height = 0;
       double originX = 0, originY = 0;

       bool isEmpty() const;
       RenderRequest intersect(const RenderRequest& other) const;
       RenderRequest expand(int margin) const;
   };
   ```

2. **NodeGraphEvaluator への追加**
   ```cpp
   // 事前計算データ
   std::map<std::string, AffinePreparedData> affinePreparedCache;
   std::map<std::string, FilterPreparedData> filterPreparedCache;

   // 要求キャッシュ
   std::map<std::string, RenderRequest> nodeRequestCache;

   // 3段階API
   void prepare(const RenderContext& context);           // 段階0
   void propagateRequests(const RenderRequest& tile);    // 段階1
   ViewPort evaluateTile(const RenderRequest& tile);     // 段階2
   ```

### Phase 2: 事前準備（段階0）の実装

1. **prepare() の実装**
   - 出力全体サイズを各ノードに伝播
   - 各ノードタイプで事前計算を実行:
     - **affine**: 逆行列計算、固定小数点変換
     - **filter**: カーネル準備
     - **image**: 画像サイズ取得

2. **prepareNode() の実装**（再帰的に上流を準備）
   ```cpp
   void prepareNode(const std::string& nodeId, const RenderContext& context);
   ```

### Phase 3: 要求伝播（段階1）の実装

1. **propagateRequests() の実装**
   - タイル（部分矩形）の要求を起点に再帰的に伝播
   - 事前計算済みパラメータを使用（再計算なし）

2. **各ノードタイプの computeInputRequest() 実装**
   - 事前計算データを参照して高速に領域変換

### Phase 4: 評価パイプライン（段階2）の改修

1. **evaluateTile() / evaluateNode() の改修**
   - `nodeRequestCache` から要求を取得
   - ViewPortサイズを要求に基づいて決定
   - 事前計算データを使用して処理

2. **applyTransform() の改修**
   - 出力サイズと事前計算データをパラメータで受け取る
   - 逆行列計算をスキップ

### Phase 5: タイル分割対応

1. **evaluateGraph() の改修**
   ```cpp
   Image evaluateGraph() {
       RenderContext context = { canvasWidth, canvasHeight, dstOriginX, dstOriginY };

       // 段階0: 事前準備（1回）
       prepare(context);

       // タイルなし（従来互換）の場合
       if (context.strategy == TileStrategy::None) {
           RenderRequest fullRequest = { 0, 0, canvasWidth, canvasHeight, ... };
           propagateRequests(fullRequest);
           return evaluateTile(fullRequest).toImage();
       }

       // タイル分割処理
       Image result(canvasWidth, canvasHeight);
       for (int ty = 0; ty < context.getTileCountY(); ty++) {
           for (int tx = 0; tx < context.getTileCountX(); tx++) {
               RenderRequest tile = context.getTileRequest(tx, ty);
               propagateRequests(tile);
               ViewPort tileResult = evaluateTile(tile);
               // 結果を result に転送
               copyTileToImage(tileResult, result, tile.x, tile.y);
           }
       }
       return result;
   }
   ```

2. **メモリ管理**
   - タイルごとに中間バッファを解放
   - 最大同時使用メモリの制限

### Phase 6: テスト・最適化

1. **既存テストの更新**
2. **新規テストケース追加**
   - 小キャンバス + 大画像
   - 回転時の領域計算
   - タイル境界でのピクセル整合性
3. **メモリ使用量比較**
4. **タイルサイズ別パフォーマンス測定**

## 注意点

### 循環参照

現在の実装と同様、`visited` セットで循環検出を維持。

### キャッシュ戦略

- 要求が変わらない場合は結果をキャッシュ可能
- 要求が変わった場合は再評価が必要

### srcOrigin の扱い

- 要求領域ベースの座標系では、srcOrigin の意味が変わる
- 「ViewPort内での原点位置」から「要求領域内での原点位置」に

## 図解

### 現状

```
image (128x128)
    ↓
affine (canvasSize=200x200 で固定)  ← 問題！
    ↓
composite
    ↓
output (200x200)
```

### 改善後

```
【パス1: 要求伝播】
output: (0,0)-(200,200), origin=(100,100)
    ↓
composite: (0,0)-(200,200), origin=(100,100)
    ↓
affine: 逆変換で入力要求を算出
    ↓
image: 画像領域と交差

【パス2: 評価】
image: 要求領域のみ読み込み
    ↓
affine: 要求サイズのViewPortに描画
    ↓
composite: 必要領域のみ合成
    ↓
output: 最終出力
```

## 関連ファイル

- `src/node_graph.h`: RenderRequest定義、メソッド宣言
- `src/node_graph.cpp`: 要求伝播・評価ロジック
- `src/image_processor.cpp`: applyTransform の改修（必要に応じて）
- `test/`: テストコード

---

## 現状のコード構造（参照用）

### GraphNode 構造体 (`node_graph.h:59-82`)

```cpp
struct GraphNode {
    std::string type;  // "image", "filter", "composite", "affine", "output"
    std::string id;

    // image用
    int imageId;
    double srcOriginX, srcOriginY;  // 正規化座標 0.0〜1.0

    // filter用
    std::string filterType;
    std::vector<float> filterParams;
    bool independent;

    // composite用
    std::vector<CompositeInput> compositeInputs;

    // affine用
    AffineMatrix affineMatrix;
};
```

### NodeGraphEvaluator クラス (`node_graph.h:96-140`)

```cpp
class NodeGraphEvaluator {
public:
    void registerImage(int imageId, const Image& img);
    void setNodes(const std::vector<GraphNode>& nodes);
    void setConnections(const std::vector<GraphConnection>& connections);
    Image evaluateGraph();
    void setCanvasSize(int width, int height);
    void setDstOrigin(double x, double y);

private:
    int canvasWidth, canvasHeight;
    double dstOriginX, dstOriginY;
    ImageProcessor processor;

    std::vector<GraphNode> nodes;
    std::vector<GraphConnection> connections;
    std::map<int, ViewPort> imageLibrary;        // 画像ライブラリ
    std::map<std::string, ViewPort> nodeResultCache;  // 評価結果キャッシュ

    ViewPort evaluateNode(const std::string& nodeId, std::set<std::string>& visited);
};
```

### evaluateNode() の処理フロー (`node_graph.cpp:44-224`)

1. 循環参照チェック
2. キャッシュ確認
3. ノードタイプ別処理:
   - **image**: `imageLibrary` から ViewPort 取得、srcOrigin 設定
   - **filter**: 入力評価 → `processor.applyFilter()`
   - **composite**: 各入力評価 → `processor.mergeImages()`
   - **affine**: 入力評価 → `processor.applyTransform()` → srcOrigin 計算
4. キャッシュに保存

### applyTransform() (`image_processor.cpp:122-284`)

**問題箇所**: 出力ViewPortサイズが `canvasWidth × canvasHeight` で固定

```cpp
ViewPort ImageProcessor::applyTransform(...) const {
    ViewPort output(canvasWidth, canvasHeight, ...);  // ← ここが問題
    // ...
}
```

### 追加が必要な要素

1. **RenderRequest 構造体**: 要求領域 + 基準座標
2. **nodeRequestCache**: 各ノードの要求キャッシュ
3. **propagateRequests()**: 要求伝播（パス1）
4. **evaluateNode() の改修**: 要求に基づく評価（パス2）

## 変更履歴

| 日付 | 内容 |
|------|------|
| 2026-01-06 | 初版作成 |
| 2026-01-06 | タイル分割処理対応を追加（組込み環境移植性考慮） |
| 2026-01-06 | Phase 1-5 実装完了、画像切れ問題解決 |
