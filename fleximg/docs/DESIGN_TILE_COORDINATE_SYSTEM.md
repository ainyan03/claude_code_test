# パイプライン座標系 設計ドキュメント

## 概要

本ドキュメントでは、fleximg のパイプライン処理における座標系の定義と、各ノードでの座標の扱いについて説明します。

**ステータス**: ✅ **実装完了**

---

## 基本原則

### 原則1: 基準相対座標系

すべての座標は「基準点を 0 とした相対位置」で表現します。

- **絶対座標という概念は不要**
- 左/上方向が負、右/下方向が正
- 各ノードは「基準点からの相対位置」のみを扱う

### 原則2: 各ノードに必要な情報の最小化

各ノードが必要とする情報は以下の2つのみ：

1. **下流要求 (RenderRequest)**: 必要なサイズと基準点位置
2. **上流応答 (ViewPort)**: 画像データ、サイズ、基準相対座標

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

---

## データ構造

### RenderRequest（下流からの要求）

```cpp
struct RenderRequest {
    int x, y;           // 要求範囲の左上座標（基準相対座標）
    int width, height;  // 要求範囲のサイズ
    double originX;     // バッファ内での基準点X位置
    double originY;     // バッファ内での基準点Y位置
};
```

**座標の意味:**
- `x, y`: 要求範囲の左上の基準相対座標
- `originX, originY`: バッファ内での基準点位置（バッファ座標）

**重要な関係:**
```
バッファの左上 (0, 0) の基準相対座標 = -originX
バッファの (originX, originY) の基準相対座標 = (0, 0)
```

### ViewPort.srcOriginX/Y（上流からの応答）

```cpp
// ViewPort のメンバー
double srcOriginX;  // 基準点から見た画像左上の相対座標
double srcOriginY;
```

**定義**: 基準点を原点 (0, 0) として、画像の左上がどこにあるか

**具体例:**
| 画像サイズ | 基準点位置 | srcOriginX | srcOriginY |
|-----------|-----------|------------|------------|
| 100x100 | 左上 (0, 0) | 0 | 0 |
| 100x100 | 中央 (0.5, 0.5) | -50 | -50 |
| 100x100 | 右下 (1, 1) | -100 | -100 |
| 8x8 | 右下 | -8 | -8 |

---

## 各ノードでの座標の扱い

### ImageEvalNode（画像ノード）

**動作**: request を無視し、全画像を返す

```cpp
ViewPort ImageEvalNode::evaluate(const RenderRequest& request, ...) {
    ViewPort result = *imageData;
    // 9点セレクタ (0,0)=左上, (0.5,0.5)=中央, (1,1)=右下
    result.srcOriginX = -srcOriginX * result.width;
    result.srcOriginY = -srcOriginY * result.height;
    return result;
}
```

**例**: 100x100画像、中央基準 → `srcOriginX = -50`

### FilterEvalNode（フィルタノード）

**動作**: 入力の座標情報をそのまま引き継ぐ

```cpp
ViewPort FilterEvalNode::evaluate(const RenderRequest& request, ...) {
    RenderRequest inputReq = computeInputRequest(request);
    ViewPort input = inputs[0]->evaluate(inputReq, context);

    ViewPort result = op->apply({input}, request);
    result.srcOriginX = input.srcOriginX;  // 座標情報を引き継ぐ
    result.srcOriginY = input.srcOriginY;
    return result;
}
```

### CompositeEvalNode（合成ノード）

**動作**: 複数入力を基準点を合わせて合成

**CompositeOperator での配置計算:**
```cpp
// refX: バッファ内での基準点位置
double refX = request.originX;

// img.srcOriginX: 基準点から見た画像左上の相対座標
// 配置位置 = 基準位置 + 相対座標
int offsetX = static_cast<int>(refX + img.srcOriginX);
```

**出力の srcOriginX:**
```cpp
// 出力バッファの左上は基準から見て -originX の位置
result.srcOriginX = -refX;
```

### AffineEvalNode（アフィン変換ノード）

**入力要求の計算 (computeInputRequest):**

```cpp
RenderRequest computeInputRequest(const RenderRequest& outputRequest) {
    // 出力要求の4頂点を逆変換してAABBを計算
    // ...

    int reqX = static_cast<int>(std::floor(minX));  // 基準相対座標

    return RenderRequest{
        reqX, reqY, width, height,
        // originX = バッファ内での基準点位置
        // バッファの x=0 が基準相対座標 reqX に対応
        // 基準相対座標 0 はバッファの x=-reqX に対応
        static_cast<double>(-reqX),
        static_cast<double>(-reqY)
    };
}
```

**AffineOperator での座標変換:**

```cpp
// パラメータの意味（間接的な計算）:
// - inputSrcOriginX_: 入力の基準相対座標
// - outputOriginX_: オフセット値（= 実際のoutputOriginX - inputSrcOriginX）

// 実際の outputOriginX を復元
int32_t outputOriginXInt = inputSrcOriginX_ + outputOriginX_;

// 座標変換式:
// sx = invA*dx + invB*dy + (invTx - invA*outputOriginX - invB*outputOriginY - inputSrcOriginX)
```

---

## タイル分割処理

### タイル要求の生成

```cpp
RenderRequest tileRequest{
    tileX, tileY,           // タイル左上のキャンバス座標
    tileW, tileH,           // タイルサイズ
    canvasOriginX - tileX,  // タイル内での基準点X位置
    canvasOriginY - tileY   // タイル内での基準点Y位置
};
```

### 具体例：8px幅のキャンバス、2px単位のタイル分割

```
キャンバス: width=8, 基準点X=4（バッファ座標4が基準点）
元画像:     width=6, 中央基準（srcOriginX=-3）, ピクセル列=[ABCDEF]

元画像の基準相対位置:
[A  B  C  D  E  F ]
-3 -2 -1  0 +1 +2

タイル要求と応答（2px単位）:
タイル0: バッファ[0,2), originX=4  → 基準相対[-4,-2)
タイル1: バッファ[2,4), originX=2  → 基準相対[-2, 0)
タイル2: バッファ[4,6), originX=0  → 基準相対[ 0,+2)
タイル3: バッファ[6,8), originX=-2 → 基準相対[+2,+4)

画像(srcOriginX=-3)との合成:
タイル0: offset = 4 + (-3) = 1  → [_ A]
タイル1: offset = 2 + (-3) = -1 → [B C]
タイル2: offset = 0 + (-3) = -3 → [D E]
タイル3: offset = -2 + (-3) = -5 → [F _]
```

---

## アフィン変換と平行移動

### 基準点中心の変換

アフィン変換は基準点 (0, 0) を中心に適用されます。

```
元画像（srcOriginX=-3）: [A B C D E F]
基準相対位置:            -3 -2 -1  0 +1 +2

↓ tx=+1 で平行移動（基準点中心）

変換後:                 [A B C D E F]
基準相対位置:            -2 -1  0 +1 +2 +3

出力タイル要求: 基準相対[-2,0)  →  応答 [A B]
出力タイル要求: 基準相対[ 0,+2) →  応答 [C D]
```

### タイル分割 + 平行移動

各タイルで独立して処理されますが、基準相対座標系を使うことで一貫した結果が得られます。

**重要**: `computeInputRequest` で `originX = -reqX` と設定することで、上流ノード（合成ノード等）が正しい範囲をカバーするバッファを作成できます。

---

## 設計の要点

### なぜ基準相対座標系か？

1. **タイル独立性**: 各タイルが独立して処理可能
2. **一貫性**: 変換の有無に関わらず同じ座標系で計算
3. **シンプルさ**: 絶対座標への変換が不要

### originX の二つの意味

| コンテキスト | originX の意味 |
|------------|---------------|
| RenderRequest | バッファ内での基準点位置 |
| ViewPort.srcOriginX | 基準点から見た画像左上の相対座標 |

**符号が逆になる理由:**
- `request.originX = 64`: 基準点がバッファの x=64 にある
- `viewport.srcOriginX = -64`: 画像左上は基準点から -64 の位置

---

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/evaluation_node.cpp` | 各EvalNodeの実装 |
| `src/fleximg/operators.cpp` | オペレータの実装 |
| `src/fleximg/node_graph.cpp` | タイル分割処理 |
| `src/fleximg/node_graph.h` | RenderRequest定義 |
| `src/fleximg/viewport.h` | ViewPort定義 |

---

## 変更履歴

| 日付 | 内容 |
|------|------|
| 2025-01-06 | 初版作成（リファクタリング設計） |
| 2026-01-07 | 基準相対座標系への完全移行完了、ドキュメント全面改訂 |
