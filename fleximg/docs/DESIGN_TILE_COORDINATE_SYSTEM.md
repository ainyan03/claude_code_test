# タイル座標系設計

パイプライン処理における座標系の定義と、各ノードでの座標の扱いについて説明します。

## 基本原則

### 原則1: 基準相対座標系

すべての座標は「基準点を 0 とした相対位置」で表現します。

- **絶対座標という概念は不要**
- 左/上方向が負、右/下方向が正
- 各ノードは「基準点からの相対位置」のみを扱う

### 原則2: 各ノードに必要な情報の最小化

各ノードが必要とする情報は以下の2つのみ：

1. **下流要求 (RenderRequest)**: 必要なサイズと基準点位置
2. **上流応答 (EvalResult)**: 画像データ、サイズ、基準相対座標

### 原則3: 下流→上流への要求伝播

```
出力ノード → アフィンノード → 合成ノード → 画像ノード
    │              │              │
    │  要求伝播    │              │
    └──────────────┴──────────────┘
```

各ノードは：
1. 下流から `RenderRequest` を受け取る
2. 自身の逆変換を適用して上流に伝播
3. 上流から結果を受け取り、変換を適用して下流に返す

## データ構造

### RenderRequest（下流からの要求）

```cpp
struct RenderRequest {
    int width, height;      // 要求範囲のサイズ
    float originX, originY; // バッファ内での基準点位置
};
```

**座標の意味:**
- `originX, originY`: バッファ内での基準点位置（バッファ座標）

**重要な関係:**
```
バッファの左上 (0, 0) の基準相対座標 = -originX
バッファの (originX, originY) の基準相対座標 = (0, 0)
```

### EvalResult.origin（上流からの応答）

```cpp
struct Point2f {
    float x, y;  // 基準点から見た画像左上の相対座標
};
```

**定義**: 基準点を原点 (0, 0) として、画像の左上がどこにあるか

**具体例:**
| 画像サイズ | 基準点位置 | origin.x | origin.y |
|-----------|-----------|----------|----------|
| 100x100 | 左上 (0, 0) | 0 | 0 |
| 100x100 | 中央 (0.5, 0.5) | -50 | -50 |
| 100x100 | 右下 (1, 1) | -100 | -100 |

## 各ノードでの座標の扱い

### ImageEvalNode（画像ノード）

request を無視し、全画像を返します。

```cpp
EvalResult ImageEvalNode::evaluate(const RenderRequest& request, ...) {
    // 9点セレクタ (0,0)=左上, (0.5,0.5)=中央, (1,1)=右下
    Point2f origin;
    origin.x = -srcOriginX * imageWidth;
    origin.y = -srcOriginY * imageHeight;
    return EvalResult{imageBuffer, origin};
}
```

**例**: 100x100画像、中央基準 → `origin.x = -50`

### FilterEvalNode（フィルタノード）

入力の座標情報をそのまま引き継ぎます。

### CompositeEvalNode（合成ノード）

複数入力を基準点を合わせて合成します。

**配置計算:**
```cpp
// refX: バッファ内での基準点位置
float refX = request.originX;

// input.origin.x: 基準点から見た画像左上の相対座標
// 配置位置 = 基準位置 + 相対座標
int offsetX = static_cast<int>(refX + input.origin.x);
```

### AffineEvalNode（アフィン変換ノード）

**入力要求の計算**: 出力要求の4頂点を逆変換してAABBを計算

アフィン変換は基準点 (0, 0) を中心に適用されます。

## タイル分割処理

### タイル要求の生成

```cpp
RenderRequest tileRequest{
    tileW, tileH,           // タイルサイズ
    canvasOriginX - tileX,  // タイル内での基準点X位置
    canvasOriginY - tileY   // タイル内での基準点Y位置
};
```

### 具体例：8px幅のキャンバス、2px単位のタイル分割

```
キャンバス: width=8, 基準点X=4（バッファ座標4が基準点）
元画像:     width=6, 中央基準（origin.x=-3）, ピクセル列=[ABCDEF]

元画像の基準相対位置:
[A  B  C  D  E  F ]
-3 -2 -1  0 +1 +2

タイル要求と応答（2px単位）:
タイル0: バッファ[0,2), originX=4  → 基準相対[-4,-2)
タイル1: バッファ[2,4), originX=2  → 基準相対[-2, 0)
タイル2: バッファ[4,6), originX=0  → 基準相対[ 0,+2)
タイル3: バッファ[6,8), originX=-2 → 基準相対[+2,+4)

画像(origin.x=-3)との合成:
タイル0: offset = 4 + (-3) = 1  → [_ A]
タイル1: offset = 2 + (-3) = -1 → [B C]
タイル2: offset = 0 + (-3) = -3 → [D E]
タイル3: offset = -2 + (-3) = -5 → [F _]
```

## 設計の要点

### なぜ基準相対座標系か？

1. **タイル独立性**: 各タイルが独立して処理可能
2. **一貫性**: 変換の有無に関わらず同じ座標系で計算
3. **シンプルさ**: 絶対座標への変換が不要

### originX の二つの意味

| コンテキスト | originX の意味 |
|------------|---------------|
| RenderRequest | バッファ内での基準点位置 |
| EvalResult.origin | 基準点から見た画像左上の相対座標 |

**符号が逆になる理由:**
- `request.originX = 64`: 基準点がバッファの x=64 にある
- `result.origin.x = -64`: 画像左上は基準点から -64 の位置

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/evaluation_node.cpp` | 各EvalNodeの実装 |
| `src/fleximg/operators.cpp` | オペレータの実装 |
| `src/fleximg/node_graph.cpp` | タイル分割処理 |
| `src/fleximg/node_graph.h` | RenderRequest定義 |
| `src/fleximg/eval_result.h` | EvalResult定義 |
