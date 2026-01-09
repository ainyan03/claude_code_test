# アーキテクチャ概要

fleximg の C++ コアライブラリの設計について説明します。

## 処理パイプライン

ノードグラフは以下の3段階で評価されます：

```
【段階1: パイプライン構築】（ノード構成変更時のみ）
GraphNode/GraphConnection → EvaluationNode のポインタグラフに変換

【段階2: 描画準備】（evaluateGraph 開始時に1回）
逆行列計算、カーネル準備など

【段階3: タイル描画】（タイル分割ループ）
出力ノードから再帰的に evaluate() を呼び出し
```

### 処理フロー

```
RenderRequest (要求)
    │
    ▼
OutputNode.evaluate()
    │ computeInputRequest()
    ▼
AffineNode.evaluate()
    │ computeInputRequest()
    ▼
ImageNode.evaluate()
    │
    │ return EvalResult (画像データ + 座標)
    ▼
AffineNode: 変換適用
    │
    │ return EvalResult
    ▼
OutputNode: 最終結果
```

## データ型

### 3つの主要な型

```
┌─────────────────────────────────────────────────────────┐
│  ViewPort（純粋ビュー）                                   │
│  - data, formatID, stride, width, height                │
│  - blendFirst(), blendOnto()                            │
│  - 所有権なし、軽量                                       │
└─────────────────────────────────────────────────────────┘
            ▲
            │ 包含
┌─────────────────────────────────────────────────────────┐
│  ImageBuffer（メモリ所有）                                │
│  - ViewPort を内包                                       │
│  - capacity, allocator, ownsData                        │
│  - RAII によるメモリ管理                                  │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  EvalResult（パイプライン評価結果）                        │
│  - ImageBuffer buffer                                   │
│  - Point2f origin（基準点からの相対座標）                  │
└─────────────────────────────────────────────────────────┘
```

### 役割の違い

| 型 | 責務 | 所有権 |
|---|------|--------|
| ViewPort | 画像データへのアクセス | なし |
| ImageBuffer | メモリの確保・解放 | あり |
| EvalResult | パイプライン処理結果と座標 | ImageBuffer を所有 |

## オペレーターシステム

### クラス階層

```
NodeOperator (抽象基底クラス)
│
├── SingleInputOperator (1入力用基底クラス)
│   ├── BrightnessOperator
│   ├── GrayscaleOperator
│   ├── BoxBlurOperator
│   ├── AlphaOperator
│   └── AffineOperator
│
└── CompositeOperator (複数入力)
```

### 主なメソッド

```cpp
class NodeOperator {
public:
    // 入力から出力を生成
    virtual EvalResult apply(const OperatorInput& input,
                             const RenderRequest& request) const = 0;

    // 入力要求を計算（フィルタ用の拡大など）
    virtual RenderRequest computeInputRequest(
        const RenderRequest& outputRequest) const;
};
```

## 評価ノード

```
EvaluationNode (基底クラス)
│
├── ImageEvalNode      # 画像データを返す（終端）
├── FilterEvalNode     # フィルタ処理を適用
├── AffineEvalNode     # アフィン変換を適用
├── CompositeEvalNode  # 複数入力を合成
└── OutputEvalNode     # 出力ノード（透過）
```

各ノードは `evaluate()` メソッドで上流ノードを再帰的に評価します。

## ノードグラフAPI

### 基本的な使用方法

```cpp
NodeGraphEvaluator evaluator(outputWidth, outputHeight);

// 画像を登録（id=0: 入力, id=1: 出力）
evaluator.registerImage(0, inputData, inputWidth, inputHeight);
evaluator.registerImage(1, outputData, outputWidth, outputHeight);

// 出力の基準点を設定
evaluator.setDstOrigin(originX, originY);

// ノードを設定
std::vector<GraphNode> nodes(3);
nodes[0].type = "image";
nodes[0].id = "img";
nodes[0].imageId = 0;
nodes[0].srcOriginX = inputWidth / 2.0f;  // 中央基準（ピクセル座標）
nodes[0].srcOriginY = inputHeight / 2.0f;

nodes[1].type = "affine";
nodes[1].id = "affine";
nodes[1].affineMatrix = matrix;

nodes[2].type = "output";
nodes[2].id = "out";
nodes[2].imageId = 1;

evaluator.setNodes(nodes);

// 接続を設定
std::vector<GraphConnection> connections = {
    {"img", "output", "affine", "in"},
    {"affine", "output", "out", "in"},
};
evaluator.setConnections(connections);

// 評価実行（結果はoutputDataに書き込まれる）
evaluator.evaluateGraph();
```

### 注意点

| 項目 | 説明 |
|------|------|
| srcOriginX/Y | **ピクセル座標**（画像内での基準点位置）。0=左上、width/2=中央X、height/2=中央Y |
| 入力ポート名 | 単一入力ノードは `"in"`。Compositeノードは `compositeInputs[].id` |
| 出力ポート名 | すべてのノードで `"output"` |

**srcOriginの解釈:**
```cpp
// ImageEvalNode での座標計算
float imgLeft = -srcOriginX;   // 50 → -50（画像左上は基準点の左50px）
float imgTop = -srcOriginY;    // 50 → -50（画像上端は基準点の上50px）
```

**JS側での変換（UI層）:**
```javascript
// 9点セレクタ（0-1正規化値）からピクセル座標に変換
const pixelOriginX = normalizedOriginX * imageWidth;
const pixelOriginY = normalizedOriginY * imageHeight;
```

## ピクセルフォーマット

### 使用するフォーマット

| フォーマット | 用途 |
|-------------|------|
| RGBA8_Straight | 入出力、画像保存 |
| RGBA16_Premultiplied | 合成・アフィン変換処理 |

### 変換方式

```cpp
// RGBA8_Straight → RGBA16_Premultiplied
A_tmp = A8 + 1;          // 1-256（ゼロ除算回避）
A16 = 255 * A_tmp;       // 255-65280
R16 = R8 * A_tmp;        // 乗算のみ、除算なし

// RGBA16_Premultiplied → RGBA8_Straight
A8 = A16 >> 8;           // 0-255
A_tmp = A8 + 1;          // 1-256
R8 = R16 / A_tmp;        // 除数範囲が限定的
```

## 座標系

### 基準相対座標系

すべての座標は「基準点を 0 とした相対位置」で表現します。

- 絶対座標という概念は不要
- 左/上方向が負、右/下方向が正
- 各ノードは「基準点からの相対位置」のみを扱う

### 主要な座標構造体

```cpp
// 下流からの要求
struct RenderRequest {
    int width, height;    // 要求サイズ
    float originX, originY;  // バッファ内での基準点位置
};

// 評価結果の座標
struct Point2f {
    float x, y;  // 基準点から見た出力左上の相対座標
};
```

### 座標の関係

```
バッファの左上 (0, 0) の基準相対座標 = -originX
バッファの (originX, originY) の基準相対座標 = (0, 0)
```

## タイル分割処理

メモリ制約のある環境向けに、キャンバスをタイルに分割して処理できます。

```cpp
// タイルサイズを設定（0 = 分割なし）
void setTileSize(int width, int height);

// デバッグ用市松模様スキップを設定
void setDebugCheckerboard(bool enabled);
```

**サイズ指定の例：**
- `(0, 0)` - 分割なし（一括処理）
- `(0, 1)` - スキャンライン（1行ずつ）
- `(16, 16)` - 16×16タイル
- `(64, 64)` - 64×64タイル

各タイルは独立して処理され、基準相対座標系により一貫した結果が得られます。

## ファイル構成

```
src/fleximg/
├── common.h              # 共通定義（Point2f など）
├── image_types.h         # 基本型（Image, AffineMatrix）
├── image_allocator.h     # カスタムアロケータ
├── pixel_format.h        # ピクセルフォーマット定義
├── pixel_format_registry.h/cpp  # フォーマット変換
├── viewport.h/cpp        # ViewPort
├── image_buffer.h/cpp    # ImageBuffer
├── eval_result.h         # EvalResult
├── operators.h/cpp       # オペレーター群
├── evaluation_node.h/cpp # 評価ノード
└── node_graph.h/cpp      # ノードグラフエンジン
```

## パフォーマンス計測

デバッグビルド（`--debug`フラグ）では、ノードタイプ別のパフォーマンス計測が有効になります。

### 計測項目

| 項目 | 説明 |
|------|------|
| time_us | 処理時間（マイクロ秒） |
| count | 呼び出し回数 |
| requestedPixels | 上流に要求したピクセル数 |
| usedPixels | 実際に使用したピクセル数 |
| wasteRatio | 不要ピクセル率（1 - used/requested） |

### ピクセル効率

タイル分割処理やアフィン変換では、出力に必要な範囲を逆算して上流に要求します。
この際、AABBで近似するため余分なピクセルを要求することがあります。

- **アフィン変換**: 45度回転時、スキャンライン分割では効率が1%以下に低下
- **ブラーフィルタ**: 半径Rの場合、入力は出力より各辺2R拡大される

効率はブラウザのコンソールログで確認できます：
```
[Perf] Affine: 1.23ms (x4) [eff:48.9%]
```

## 関連ドキュメント

詳細な設計については以下を参照：

- [DESIGN_TILE_COORDINATE_SYSTEM.md](DESIGN_TILE_COORDINATE_SYSTEM.md): 座標系の詳細
- [DESIGN_VIEWPORT_REFACTOR.md](DESIGN_VIEWPORT_REFACTOR.md): 型構造の詳細
- [DESIGN_ALPHA_CONVERSION.md](DESIGN_ALPHA_CONVERSION.md): ピクセルフォーマット変換の詳細
- [DESIGN_PERF_METRICS.md](DESIGN_PERF_METRICS.md): パフォーマンス計測の詳細
