# アーキテクチャ概要

fleximg の C++ コアライブラリの設計について説明します。

## 処理パイプライン

ノードグラフは RendererNode を発火点として評価されます。

```
【処理フロー】
SourceNode（画像入力）
    │
    │ pullProcess()（上流からプル）
    ▼
TransformNode / フィルタノード
    │
    │ pullProcess()
    ▼
RendererNode（発火点）
    │
    │ pushProcess()（下流へプッシュ）
    ▼
SinkNode（出力先）
```

### タイル分割

メモリ制約のある環境向けに、キャンバスをタイルに分割して処理できます。

```cpp
RendererNode renderer;
renderer.setVirtualScreen(640, 480, 320, 240);  // 幅, 高さ, 基準X, 基準Y
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
├── SourceNode        # 画像データを提供
├── SinkNode          # 出力先を保持
├── TransformNode     # アフィン変換
├── FilterNodeBase    # フィルタ共通基底
│   ├── BrightnessNode   # 明るさ調整
│   ├── GrayscaleNode    # グレースケール
│   ├── BoxBlurNode      # ぼかし
│   └── AlphaNode        # アルファ調整
├── CompositeNode     # 複数入力の合成
└── RendererNode      # パイプライン実行の発火点
```

### 接続方式

ノード間は Port オブジェクトで接続します。`>>` 演算子でチェーン接続も可能です。

```cpp
SourceNode source(imageView);
TransformNode transform;
RendererNode renderer;
SinkNode sink(outputView, canvasWidth / 2, canvasHeight / 2);

// 接続: source → transform → renderer → sink
source >> transform >> renderer >> sink;

// 実行
renderer.setVirtualScreen(canvasWidth, canvasHeight, canvasWidth / 2, canvasHeight / 2);
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
    int width, height;  // 要求サイズ
    Point2f origin;     // バッファ内での基準点位置
};

// 評価結果
struct RenderResult {
    ImageBuffer buffer;
    Point2f origin;  // バッファ内での基準点位置
};
```

### 座標の意味

`origin` はバッファ内での基準点のピクセル位置を表します。

```
例: 32x24画像、右下隅を基準点とする場合
  origin.x = 32  (右端)
  origin.y = 24  (下端)

例: 100x100画像、中央を基準点とする場合
  origin.x = 50
  origin.y = 50
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
├── perf_metrics.h        # パフォーマンス計測
├── nodes/
│   ├── source_node.h       # SourceNode
│   ├── sink_node.h         # SinkNode
│   ├── transform_node.h    # TransformNode
│   ├── filter_node_base.h  # FilterNodeBase（フィルタ共通基底）
│   ├── brightness_node.h   # BrightnessNode
│   ├── grayscale_node.h    # GrayscaleNode
│   ├── box_blur_node.h     # BoxBlurNode
│   ├── alpha_node.h        # AlphaNode
│   ├── composite_node.h    # CompositeNode
│   └── renderer_node.h     # RendererNode（発火点）
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
#include "fleximg/nodes/renderer_node.h"

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

RendererNode renderer;
renderer.setVirtualScreen(320, 240, 160, 120);  // キャンバス中央を基準点に

SinkNode sink(outputView, 160, 120);  // キャンバス中央

// 接続
source >> transform >> renderer >> sink;

// 実行
renderer.exec();
// 結果は outputBuffer に書き込まれる
```

### タイル分割処理

```cpp
RendererNode renderer;
renderer.setVirtualScreen(1920, 1080, 960, 540);
renderer.setTileConfig(TileConfig{64, 64});
renderer.exec();
```

## 関連ドキュメント

- [DESIGN_RENDERER_NODE.md](DESIGN_RENDERER_NODE.md) - RendererNode 設計詳細
- [DESIGN_FILTER_NODES.md](DESIGN_FILTER_NODES.md) - フィルタノード設計
- [DESIGN_TYPE_STRUCTURE.md](DESIGN_TYPE_STRUCTURE.md) - 型構造設計
- [DESIGN_PIXEL_FORMAT.md](DESIGN_PIXEL_FORMAT.md) - ピクセルフォーマット変換
- [DESIGN_PERF_METRICS.md](DESIGN_PERF_METRICS.md) - パフォーマンス計測
- [GITHUB_PAGES_SETUP.md](GITHUB_PAGES_SETUP.md) - GitHub Pages セットアップ
