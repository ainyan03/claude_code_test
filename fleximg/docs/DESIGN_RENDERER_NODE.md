# RendererNode 設計

## 概要

RendererNode はパイプライン実行の発火点となるノードです。ノードグラフの中央に位置し、上流側からプル型で画像を取得し、下流側へプッシュ型で配布します。

```
上流側（プル型）              下流側（プッシュ型）
─────────────────            ─────────────────
SourceNode                    SinkNode
    ↑ pullProcess()              ↓ pushProcess()
AffineNode           →      RendererNode
    ↑ pullProcess()           (発火点)
フィルタノード
```

## 基本的な使い方

### 単一出力パイプライン

```cpp
#include "fleximg/nodes/source_node.h"
#include "fleximg/nodes/sink_node.h"
#include "fleximg/nodes/affine_node.h"
#include "fleximg/nodes/renderer_node.h"

// ノード作成
SourceNode src(imageView);
src.setOrigin(to_fixed8(imageView.width / 2), to_fixed8(imageView.height / 2));

AffineNode affine;
affine.setMatrix(AffineMatrix::rotate(0.5f));

RendererNode renderer;
renderer.setVirtualScreen(320, 240, 160, 120);  // 幅, 高さ, 基準X, 基準Y

SinkNode sink(outputView, 160, 120);  // 出力先, 基準X, 基準Y

// パイプライン構築
src >> affine >> renderer >> sink;

// 実行
renderer.exec();
```

### スキャンライン処理（必須仕様）

**パイプライン上を流れるリクエストは必ず高さ1ピクセル（スキャンライン）です。**

```cpp
RendererNode renderer;
renderer.setVirtualScreen(1920, 1080, 960, 540);
renderer.setTileConfig(TileConfig{64, 64});  // tileWidth=64のみ有効（tileHeightは無視）
renderer.exec();
```

> **Note**: `TileConfig` の `tileHeight` は無視され、常に高さ1で処理されます。
> この制約により、DDA処理の最適化や有効ピクセル範囲の事前計算が可能になります。

## 座標系

### 基準点相対座標

全ての座標は **基準点（origin）からの相対位置** で表現されます。

```
        ↑ 負 (上)
        │
負 (左) ←─●─→ 正 (右)     ● = 基準点 (origin)
        │
        ↓ 正 (下)
```

### 基準点一致ルール

**SourceNode と SinkNode の基準点が一致するように画像が配置されます。**

```
SourceNode (100x100, origin: 50,50)     SinkNode (200x200, origin: 100,100)
   ┌──────────┐                            ┌────────────────────┐
   │    ●     │                            │                    │
   │ (50,50)  │  ──基準点が一致──→        │         ●          │
   └──────────┘                            │     (100,100)      │
                                           └────────────────────┘

画像左上の基準点相対座標 = (-50, -50)
出力での画像左上位置 = (100-50, 100-50) = (50, 50)
```

### RenderRequest / RenderResult

```cpp
// 下流から上流への要求
struct RenderRequest {
    int16_t width, height;  // 要求サイズ
    Point origin;           // バッファ内での基準点位置（int_fixed8）
};

// 上流から下流への応答
struct RenderResult {
    ImageBuffer buffer;
    Point origin;  // バッファ内での基準点位置（int_fixed8）
};
```

## プル/プッシュ API

### Node 基底クラスの API

```cpp
class Node {
public:
    // ========================================
    // 共通処理（派生クラスでオーバーライド）
    // ========================================

    virtual RenderResult process(RenderResult&& input,
                                 const RenderRequest& request);
    virtual void prepare(const RenderRequest& screenInfo);
    virtual void finalize();

    // ========================================
    // プル型（上流側で使用）
    // ========================================

    virtual RenderResult pullProcess(const RenderRequest& request);
    virtual void pullPrepare(const RenderRequest& screenInfo);
    virtual void pullFinalize();

    // ========================================
    // プッシュ型（下流側で使用）
    // ========================================

    virtual void pushProcess(RenderResult&& input,
                             const RenderRequest& request);
    virtual void pushPrepare(const RenderRequest& screenInfo);
    virtual void pushFinalize();
};
```

### 処理の流れ

```
┌─────────────────────────────────────────────────────┐
│                    Node の処理                       │
├─────────────────────────────────────────────────────┤
│  pullProcess():              pushProcess(input):    │
│  ┌──────────────┐             ┌──────────────┐     │
│  │ 1. 上流から  │             │ 1. 引数から  │     │
│  │    入力取得  │             │    入力取得  │     │
│  └──────┬───────┘             └──────┬───────┘     │
│         │                            │              │
│         ▼                            ▼              │
│  ┌──────────────────────────────────────────┐      │
│  │      2. process() を呼び出し (共通処理)   │      │
│  └──────────────────────────────────────────┘      │
│         │                            │              │
│         ▼                            ▼              │
│  ┌──────────────┐             ┌──────────────┐     │
│  │ 3. 戻り値で  │             │ 3. 下流へ    │     │
│  │    返す      │             │    push      │     │
│  └──────────────┘             └──────────────┘     │
└─────────────────────────────────────────────────────┘
```

## RendererNode API

```cpp
class RendererNode : public Node {
public:
    RendererNode();

    // 仮想スクリーン設定
    void setVirtualScreen(int width, int height, float originX, float originY);

    // タイル設定
    void setTileConfig(const TileConfig& config);

    // 実行（一括）
    void exec() {
        execPrepare();
        execProcess();
        execFinalize();
    }

    // 実行（フェーズ別）
    void execPrepare();   // 準備を上流・下流に伝播
    void execProcess();   // タイル単位で処理
    void execFinalize();  // 終了を上流・下流に伝播

protected:
    // カスタマイズポイント
    virtual void processTile(int tileX, int tileY);
};
```

### 実行フローの詳細

```
exec() 呼び出し時:

1. execPrepare()
   RendererNode
     │
     ├─→ upstream->pullPrepare(screenInfo)  // 上流へ伝播
     │
     └─→ downstream->pushPrepare(screenInfo)  // 下流へ伝播

2. execProcess()
   for each scanline (ty = 0..height-1):
     for each tile (tx = 0..tileCountX-1):
       RendererNode.processTile(tx, ty)
         │
         │  tileRequest: width=tileWidth, height=1（スキャンライン）
         │
         ├─→ result = upstream->pullProcess(tileRequest)  // 上流から取得
         │
         └─→ downstream->pushProcess(result, tileRequest) // 下流へ配布

3. execFinalize()
   RendererNode
     │
     ├─→ upstream->pullFinalize()  // 上流へ伝播
     │
     └─→ downstream->pushFinalize()  // 下流へ伝播
```

## ノードの配置分類

| 分類 | 配置可能位置 | 例 |
|------|------------|-----|
| 入力端点 | 上流のみ | SourceNode |
| 出力端点 | 下流のみ | SinkNode |
| 処理ノード | 中間（上流/下流） | AffineNode, フィルタノード, CompositeNode |
| 発火点 | 中央 | RendererNode |

**注意**: タイル分割時、Renderer下流に配置したBoxBlurフィルタはタイル境界で正しく動作しません。

## カスタム拡張

### 入出力サイズが異なるノード

`pullProcess()` をオーバーライドして入力要求を変更できます：

```cpp
class BlurNode : public Node {
    RenderResult pullProcess(const RenderRequest& request) override {
        // カーネルサイズ分だけ拡大したrequestで上流を評価
        RenderRequest expandedReq = request.expand(radius_);
        RenderResult input = upstreamNode(0)->pullProcess(expandedReq);
        // 元のrequestで処理
        return process(std::move(input), request);
    }
};
```

### 独自のタイル分割戦略

`processTile()` をオーバーライドできます：

```cpp
class AdaptiveTileRenderer : public RendererNode {
protected:
    void processTile(int tileX, int tileY) override {
        // 独自の分割戦略
    }
};
```

## 複数出力対応

**DistributorNode** を使用することで、1つのパイプラインから複数の出力を生成可能です：

```
SourceNode
    │
    └─→ AffineNode
          │
          └─→ RendererNode (発火点)
                │
                └─→ DistributorNode (1入力・複数出力)
                      │
                      ├─→ SinkNode A (全体: 1920x1080)
                      ├─→ SinkNode B (左上領域: 640x360)
                      └─→ SinkNode C (プレビュー: 480x270)
```

## 関連ドキュメント

- [ARCHITECTURE.md](ARCHITECTURE.md) - 全体アーキテクチャ
