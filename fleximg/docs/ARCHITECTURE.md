# アーキテクチャ概要

fleximg の C++ コアライブラリの設計について説明します。

## 処理パイプライン

ノードグラフは Renderer クラスによって評価されます。

```
【処理フロー】
SinkNode（出力先）
    │
    │ Renderer.exec()
    ▼
タイル分割ループ
    │ processTile(tx, ty)
    ▼
上流ノードを再帰評価
    │ evaluateUpstream()
    ▼
結果を出力先にコピー
```

### タイル分割

メモリ制約のある環境向けに、キャンバスをタイルに分割して処理できます。

```cpp
Renderer renderer(sinkNode);
renderer.setTileConfig(TileConfig{64, 64});  // 64x64タイル
renderer.exec();
```

**サイズ指定の例:**
- `{0, 0}` - 分割なし（一括処理）
- `{0, 1}` - スキャンライン（1行ずつ）
- `{64, 64}` - 64x64タイル

## ノードシステム

### ノードタイプ

```
Node (基底クラス)
│
├── SourceNode      # 画像データを提供
├── SinkNode        # 出力先を保持
├── TransformNode   # アフィン変換
├── FilterNode      # フィルタ処理
└── CompositeNode   # 複数入力の合成
```

### 接続方式

ノード間は Port オブジェクトで接続します。

```cpp
SourceNode source(imageView);
TransformNode transform;
SinkNode sink(outputView, canvasWidth, canvasHeight);

// 接続: source → transform → sink
transform.input(0)->connect(source.output());
sink.input(0)->connect(transform.output());

// 実行
Renderer renderer(sink);
renderer.exec();
```

## データ型

### 主要な型

```
┌─────────────────────────────────────────────────────────┐
│  ViewPort（純粋ビュー）                                   │
│  - data, formatID, stride, width, height                │
│  - 所有権なし、軽量                                       │
└─────────────────────────────────────────────────────────┘
            ▲
            │ view() で取得
┌─────────────────────────────────────────────────────────┐
│  ImageBuffer（メモリ所有）                                │
│  - ViewPort を生成可能                                   │
│  - RAII によるメモリ管理                                  │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  RenderResult（パイプライン評価結果）                      │
│  - ImageBuffer buffer                                   │
│  - Point2f origin（基準点からの相対座標）                  │
└─────────────────────────────────────────────────────────┘
```

### 役割の違い

| 型 | 責務 | 所有権 |
|---|------|--------|
| ViewPort | 画像データへのアクセス | なし |
| ImageBuffer | メモリの確保・解放 | あり |
| RenderResult | パイプライン処理結果と座標 | ImageBuffer を所有 |

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
    int width, height;       // 要求サイズ
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

## 操作モジュール

### フィルタ (operations/filters.h)

```cpp
namespace filters {
    void brightness(ViewPort& dst, const ViewPort& src, float amount);
    void grayscale(ViewPort& dst, const ViewPort& src);
    void boxBlur(ViewPort& dst, const ViewPort& src, int radius);
    void alpha(ViewPort& dst, const ViewPort& src, float scale);
}
```

### アフィン変換 (operations/transform.h)

```cpp
namespace transform {
    void affine(ViewPort& dst, float dstOriginX, float dstOriginY,
                const ViewPort& src, float srcOriginX, float srcOriginY,
                const AffineMatrix& matrix);
}
```

### ブレンド (operations/blend.h)

```cpp
namespace blend {
    void first(ViewPort& canvas, float canvasOriginX, float canvasOriginY,
               const ViewPort& src, float srcOriginX, float srcOriginY);
    void onto(ViewPort& canvas, float canvasOriginX, float canvasOriginY,
              const ViewPort& src, float srcOriginX, float srcOriginY);
}
```

## ファイル構成

```
src/fleximg/
├── common.h              # 共通定義（Point2f, AffineMatrix）
├── pixel_format.h        # ピクセルフォーマット定義
├── image_allocator.h     # カスタムアロケータ
├── image_buffer.h        # ImageBuffer
├── viewport.h/cpp        # ViewPort
├── port.h                # Port（ノード接続）
├── node.h                # Node 基底クラス
├── render_types.h        # RenderRequest, RenderResult 等
├── renderer.h/cpp        # Renderer（パイプライン実行）
├── nodes/
│   ├── source_node.h     # SourceNode
│   ├── sink_node.h       # SinkNode
│   ├── transform_node.h  # TransformNode
│   ├── filter_node.h     # FilterNode
│   └── composite_node.h  # CompositeNode
└── operations/
    ├── transform.h/cpp   # アフィン変換
    ├── filters.h/cpp     # フィルタ処理
    └── blend.h/cpp       # ブレンド処理
```

## 使用例

### 基本的なパイプライン

```cpp
#include "fleximg/common.h"
#include "fleximg/viewport.h"
#include "fleximg/image_buffer.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/transform_node.h"
#include "fleximg/renderer.h"

using namespace fleximg;

// 入出力バッファ
ViewPort inputView = /* 入力画像 */;
ImageBuffer outputBuffer(320, 240, PixelFormatIDs::RGBA8_Straight);
ViewPort outputView = outputBuffer.view();

// ノード作成
SourceNode source(inputView);
source.setOrigin(inputView.width / 2.0f, inputView.height / 2.0f);

TransformNode transform;
transform.setMatrix(AffineMatrix::rotate(0.5f));  // 約30度回転

SinkNode sink(outputView, 320, 240);
sink.setOrigin(160, 120);  // キャンバス中央

// 接続
transform.input(0)->connect(source.output());
sink.input(0)->connect(transform.output());

// 実行
Renderer renderer(sink);
renderer.exec();
// 結果は outputBuffer に書き込まれる
```

### タイル分割処理

```cpp
Renderer renderer(sink);
renderer.setTileConfig(TileConfig{64, 64});
renderer.setDebugCheckerboard(true);  // デバッグ用市松模様
renderer.exec();
```
