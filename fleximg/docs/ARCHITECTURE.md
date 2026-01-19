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
AffineNode / フィルタノード
    │
    │ pullProcess()
    ▼
RendererNode（発火点）
    │
    │ pushProcess()（下流へプッシュ）
    ▼
[フィルタノード]（オプション）
    │
    ▼
SinkNode（出力先）
```

### スキャンライン処理（必須仕様）

**パイプライン上を流れるリクエストは必ず高さ1ピクセル（スキャンライン）です。**

この制約により、以下の最適化が可能になります：
- DDA処理を1行単位で最適化
- 有効ピクセル範囲の事前計算
- メモリ効率の向上（行単位のバッファ管理）

```cpp
RendererNode renderer;
renderer.setVirtualScreen(640, 480, 320, 240);  // 幅, 高さ, 基準X, 基準Y
renderer.setTileConfig(TileConfig{64, 64});  // tileWidth=64（tileHeightは無視）
renderer.exec();
```

**タイル幅の指定例:**
- `{0, 0}` - 横方向は分割なし（画面幅を1行ずつ処理）
- `{64, 64}` - 横64ピクセル × 高さ1行ずつ処理
- `{128, 1}` - 横128ピクセル × 高さ1行ずつ処理

> **Note**: `TileConfig` の `tileHeight` は無視され、常に高さ1で処理されます。
> 後方互換性のため設定APIは維持されていますが、指定値は効果を持ちません。

**既知の制限**: Renderer下流に配置したブラーフィルタはタイル境界で正しく動作しません。これはPush処理フローでは隣接タイルの情報を参照できないためです。ブラーフィルタを使用する場合は、Renderer上流（Pull処理）に配置してください。

## ノードシステム

### ノードタイプ

```
Node (基底クラス)
│
├── SourceNode        # 画像データを提供（入力端点）
├── SinkNode          # 出力先を保持（出力端点）
├── AffineNode        # アフィン変換（プル/プッシュ両対応）
├── FilterNodeBase    # フィルタ共通基底
│   ├── BrightnessNode      # 明るさ調整
│   ├── GrayscaleNode       # グレースケール
│   └── AlphaNode           # アルファ調整
├── HorizontalBlurNode  # 水平ぼかし（ガウシアン近似対応）
├── VerticalBlurNode    # 垂直ぼかし（ガウシアン近似対応）
├── CompositeNode     # 複数画像を合成（N入力 → 1出力）
├── MatteNode         # マット合成（3入力: 前景/背景/マスク → 1出力）
├── DistributorNode   # 画像を複数先に分配（1入力 → N出力）
└── RendererNode      # パイプライン実行の発火点
```

### CompositeNode と DistributorNode の違い

これらは対称的な構造を持つノードです。

| ノード | ポート構成 | 用途 |
|--------|-----------|------|
| CompositeNode | N入力 → 1出力 | 複数の画像を重ね合わせて1つの画像を生成 |
| DistributorNode | 1入力 → N出力 | 1つの画像を複数の出力先（SinkNode等）に配布 |

```
【CompositeNode】            【DistributorNode】
背景画像 ─┐                        ┌─→ SinkA
          ├→ CompositeNode         │
前景画像1 ─┤       ↓           RendererNode → DistributorNode
          │   合成された                │
前景画像2 ─┘   1枚の画像               └─→ SinkB
```

使用例:
```cpp
// CompositeNode: 背景に前景を重ねる
CompositeNode composite(2);  // 2入力
bgSource >> composite;                // 入力ポート0（背景）
fgSource.connectTo(composite, 1);     // 入力ポート1（前景）
composite >> renderer >> sink;

// DistributorNode: 1つの画像を複数のSinkに出力
DistributorNode distributor(2);       // 2出力
renderer >> distributor;
distributor.connectTo(sinkMain, 0, 0);     // 出力0 → メイン出力
distributor.connectTo(sinkPreview, 0, 1);  // 出力1 → プレビュー
```

### MatteNode（マット合成）

外部のアルファマスクを使って2つの画像を合成するノードです。

```
Output = Image1 × Alpha + Image2 × (1 - Alpha)
```

| 入力ポート | 役割 | 未接続時 |
|-----------|------|---------|
| 0 | 前景（マスク白部分） | 透明の黒 |
| 1 | 背景（マスク黒部分） | 透明の黒 |
| 2 | アルファマスク | alpha=0（全面背景） |

使用例:
```cpp
MatteNode matte;
foreground >> matte;               // ポート0（前景）
background.connectTo(matte, 1);    // ポート1（背景）
alphaMask.connectTo(matte, 2);     // ポート2（マスク）
matte >> renderer >> sink;
```

### 接続方式

ノード間は Port オブジェクトで接続します。3つの接続方法が利用可能です。

#### 演算子チェーン（推奨）

最も簡潔で可読性の高い接続方法です。

```cpp
// >> 演算子: 左から右へデータが流れるイメージ
source >> affine >> renderer >> sink;

// << 演算子: 右から左へ接続を記述（同じ結果）
sink << renderer << affine << source;
```

#### connectTo() メソッド

このノードの出力を、指定したノードの入力に接続します。

```cpp
source.connectTo(affine);      // source → affine
affine.connectTo(renderer);    // affine → renderer
renderer.connectTo(sink);      // renderer → sink

// 複数ポートを持つノードでは、ポートインデックスを指定可能
// connectTo(target, targetInputIndex, outputIndex)
nodeA.connectTo(composite, 1);  // nodeAをcompositeの2番目の入力に接続
```

#### connectFrom() メソッド

指定したノードの出力を、このノードの入力に接続します（connectTo の逆向き）。

```cpp
sink.connectFrom(renderer);    // renderer → sink
renderer.connectFrom(affine);  // affine → renderer
affine.connectFrom(source);    // source → affine
```

#### 使用例

```cpp
SourceNode source(imageView);
AffineNode affine;
RendererNode renderer;
SinkNode sink(outputView, canvasWidth / 2, canvasHeight / 2);

// 接続: source → affine → renderer → sink
source >> affine >> renderer >> sink;

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
│  - Point origin（バッファ内での基準点位置、int_fixed Q16.16）│
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
    int16_t width, height;  // 要求サイズ
    Point origin;           // バッファ内での基準点位置（int_fixed Q16.16）
};

// 評価結果
struct RenderResult {
    ImageBuffer buffer;
    Point origin;  // バッファ内での基準点位置（int_fixed Q16.16）
};
```

※ `Point` は固定小数点 Q16.16（`int_fixed`）をメンバに持つ構造体です。

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
    void boxBlur(ViewPort& dst, const ViewPort& src, int radius,
                 int srcOffsetX = 0, int srcOffsetY = 0);
    void alpha(ViewPort& dst, const ViewPort& src, float scale);
}
```

### アフィン変換 (operations/transform.h)

```cpp
namespace transform {
    void affine(ViewPort& dst, int_fixed dstOriginX, int_fixed dstOriginY,
                const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY,
                const FixedPointInverseMatrix& invMatrix);
}
```

### ブレンド (operations/blend.h)

```cpp
namespace blend {
    void first(ViewPort& canvas, int_fixed canvasOriginX, int_fixed canvasOriginY,
               const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY);
    void onto(ViewPort& canvas, int_fixed canvasOriginX, int_fixed canvasOriginY,
              const ViewPort& src, int_fixed srcOriginX, int_fixed srcOriginY);
}
```

※ `int_fixed` は Q16.16 固定小数点型です（`types.h` で定義）。

## ファイル構成

```
src/fleximg/
├── core/                     # コア機能（fleximg::core 名前空間）
│   ├── common.h              # NAMESPACE定義、バージョン
│   ├── types.h               # 固定小数点型、数学型、AffineMatrix
│   ├── port.h                # Port（ノード接続）
│   ├── node.h                # Node 基底クラス
│   ├── perf_metrics.h        # パフォーマンス計測
│   └── memory/               # メモリ管理（fleximg::core::memory 名前空間）
│       ├── allocator.h       # IAllocator, DefaultAllocator
│       ├── platform.h/cpp    # IPlatformMemory（組込み環境対応）
│       ├── pool_allocator.h/cpp  # PoolAllocator（ビットマップ方式）
│       └── buffer_handle.h   # BufferHandle（RAII）
│
├── image/                    # 画像処理
│   ├── pixel_format.h        # ピクセルフォーマット定義
│   ├── pixel_format_registry.h/cpp  # フォーマット変換レジストリ
│   ├── viewport.h/cpp        # ViewPort
│   ├── image_buffer.h        # ImageBuffer
│   ├── image_allocator.h     # [DEPRECATED] 旧アロケータ
│   └── render_types.h        # RenderRequest, RenderResult
│
├── nodes/
│   ├── source_node.h         # SourceNode
│   ├── sink_node.h           # SinkNode
│   ├── affine_node.h         # AffineNode
│   ├── distributor_node.h    # DistributorNode
│   ├── filter_node_base.h      # FilterNodeBase（フィルタ共通基底）
│   ├── brightness_node.h       # BrightnessNode
│   ├── grayscale_node.h        # GrayscaleNode
│   ├── horizontal_blur_node.h  # HorizontalBlurNode（水平ぼかし）
│   ├── vertical_blur_node.h    # VerticalBlurNode（垂直ぼかし）
│   ├── alpha_node.h            # AlphaNode
│   ├── composite_node.h        # CompositeNode
│   ├── matte_node.h            # MatteNode（マット合成）
│   └── renderer_node.h         # RendererNode（発火点）
│
└── operations/
    ├── transform.h/cpp       # アフィン変換
    ├── filters.h/cpp         # フィルタ処理
    └── blend.h/cpp           # ブレンド処理
```

## 使用例

### 基本的なパイプライン

```cpp
#include "fleximg/common.h"
#include "fleximg/viewport.h"
#include "fleximg/image_buffer.h"
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/affine_node.h"
#include "fleximg/nodes/renderer_node.h"

using namespace fleximg;

// 入出力バッファ
ViewPort inputView = /* 入力画像 */;
ImageBuffer outputBuffer(320, 240, PixelFormatIDs::RGBA8_Straight);
ViewPort outputView = outputBuffer.view();

// ノード作成
SourceNode source(inputView);
source.setOrigin(to_fixed(inputView.width / 2), to_fixed(inputView.height / 2));

AffineNode affine;
affine.setMatrix(AffineMatrix::rotate(0.5f));  // 約30度回転

RendererNode renderer;
renderer.setVirtualScreen(320, 240, 160, 120);  // キャンバス中央を基準点に

SinkNode sink(outputView, 160, 120);  // キャンバス中央

// 接続
source >> affine >> renderer >> sink;

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
