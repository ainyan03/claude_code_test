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
src.setPivot(to_fixed(imageView.width / 2), to_fixed(imageView.height / 2));

AffineNode affine;
affine.setMatrix(AffineMatrix::rotate(0.5f));

RendererNode renderer;
renderer.setVirtualScreen(320, 240);
renderer.setPivotCenter();  // スクリーン中央にワールド原点を配置

SinkNode sink(outputView);
sink.setPivotCenter();  // 出力バッファ中央がワールド原点

// パイプライン構築
src >> affine >> renderer >> sink;

// 実行
renderer.exec();
```

### スキャンライン処理（必須仕様）

**パイプライン上を流れるリクエストは必ず高さ1ピクセル（スキャンライン）です。**

```cpp
RendererNode renderer;
renderer.setVirtualScreen(1920, 1080);
renderer.setPivotCenter();  // または renderer.setPivot(960, 540);
renderer.setTileConfig(TileConfig{64, 64});  // tileWidth=64のみ有効（tileHeightは無視）
renderer.exec();
```

> **Note**: `TileConfig` の `tileHeight` は無視され、常に高さ1で処理されます。
> この制約により、DDA処理の最適化や有効ピクセル範囲の事前計算が可能になります。

## 座標系

### pivot とワールド座標

全ての座標は **ワールド原点からの相対位置** で表現されます。
各ノードの pivot は「ワールド原点 (0,0) に対応する点」を表します。

```
        ↑ 負 (上)
        │
負 (左) ←─●─→ 正 (右)     ● = ワールド原点 (0,0) = 各ノードの pivot
        │
        ↓ 正 (下)
```

### pivot 一致ルール

**SourceNode と SinkNode の pivot がワールド原点で出会うように画像が配置されます。**

```
SourceNode (100x100, pivot: 50,50)     SinkNode (200x200, pivot: 100,100)
   ┌──────────┐                            ┌────────────────────┐
   │    ●     │                            │                    │
   │ (50,50)  │  ──pivotがワールド原点で一致──→  │         ●          │
   └──────────┘                            │     (100,100)      │
                                           └────────────────────┘

画像左上のワールド座標 = (-50, -50)
出力での画像左上位置 = (100-50, 100-50) = (50, 50)
```

### RenderRequest / RenderResponse

```cpp
// 下流から上流への要求
struct RenderRequest {
    int16_t width, height;  // 要求サイズ
    Point origin;           // バッファ内での基準点位置（int_fixed Q16.16）
};

// 上流から下流への応答
struct RenderResponse {
    ImageBuffer buffer;
    Point origin;  // バッファ内での基準点位置（int_fixed Q16.16）
};
```

### 実例: サイズの異なる画像の配置

pivotの値によって、画像がどこに配置されるかが決まります。

**例1: 小さい画像を大きいキャンバスの中央に配置**

```
入力画像: 100x100, pivot=(50, 50)  → 画像中央がワールド原点に対応
出力キャンバス: 300x200, pivot=(150, 100) → キャンバス中央がワールド原点に対応

配置計算:
  dstX = 150 - 50 = 100  (キャンバス上のX座標)
  dstY = 100 - 50 = 50   (キャンバス上のY座標)

結果:
  ┌─────────────────────────────────┐
  │            (300x200)            │
  │                                 │
  │      ┌───────────┐              │
  │      │  (100x100) │              │
  │      │     ●     │  ←両方の pivot がワールド原点で一致
  │      └───────────┘              │
  │                                 │
  └─────────────────────────────────┘
```

**例2: 同じ画像を左上に配置**

```
入力画像: 100x100, pivot=(0, 0)    → 画像左上がワールド原点に対応
出力キャンバス: 300x200, pivot=(0, 0) → キャンバス左上がワールド原点に対応

配置計算:
  dstX = 0 - 0 = 0
  dstY = 0 - 0 = 0

結果:
  ●───────────┬─────────────────────┐
  │  (100x100) │                     │
  │            │                      │  ←両方の pivot が左上角で一致
  ├───────────┘                      │
  │            (300x200)             │
  └─────────────────────────────────┘
```

**例3: 右下に配置**

```
入力画像: 100x100, pivot=(100, 100) → 画像右下がワールド原点に対応
出力キャンバス: 300x200, pivot=(300, 200) → キャンバス右下がワールド原点に対応

配置計算:
  dstX = 300 - 100 = 200
  dstY = 200 - 100 = 100

結果:
  ┌─────────────────────────────────┐
  │            (300x200)            │
  │                                 │
  │                  ┌───────────┐  │
  │                  │  (100x100) │  │
  └──────────────────┴───────────●  ←両方の pivot が右下角で一致
```

### 実例: フィルタによるサイズ拡張

ブラーなどのフィルタはカーネルサイズ分だけ出力を拡張します。

```
入力: 100x100, pivot=(50, 50)
HorizontalBlur: radius=5

出力: 110x110
  ↑ 幅が +10 (radius×2)

意味:
  - 出力バッファは入力より左右に5ピクセル拡張
  - RenderResponse の origin はバッファ左上のワールド座標を表す
  - pivot（ワールド原点）の位置は変わらない
```

### 実例: スキャンライン処理でのorigin変化

スキャンライン処理（高さ1のバッファで1行ずつ処理）では、各行ごとに
RenderRequest/Response の origin.y が変化します。

```
画像: 100x100, pivot=(50, 50)

行0: origin.y = -50  (バッファ左上のワールドY座標)
行1: origin.y = -49
行2: origin.y = -48
...
行49: origin.y = -1
行50: origin.y = 0  (この行が pivot と同じ高さ)
行51: origin.y = 1
...
行99: origin.y = 49

ポイント:
  - origin はバッファ左上のワールド座標
  - pivot はワールド原点 (0,0) に対応する点
  - 行が下に進むほど、origin.y は増加（ワールド座標が下方向に進む）
```

## プル/プッシュ API

### Node 基底クラスの API

Template Methodパターンを採用しています。`pullProcess()` 等は `final` で共通処理を行い、
派生クラスは `onPullProcess()` 等をオーバーライドします。

```cpp
class Node {
public:
    // ========================================
    // 共通処理（派生クラスでオーバーライド可能）
    // ========================================

    virtual RenderResponse process(RenderResponse&& input,
                                 const RenderRequest& request);
    virtual void prepare(const RenderRequest& screenInfo);
    virtual void finalize();

    // ========================================
    // プル型（上流側で使用）- final メソッド
    // ========================================

    virtual RenderResponse pullProcess(const RenderRequest& request) final;
    virtual bool pullPrepare(const PrepareRequest& request) final;
    virtual void pullFinalize() final;

    // ========================================
    // プッシュ型（下流側で使用）- final メソッド
    // ========================================

    virtual void pushProcess(RenderResponse&& input,
                             const RenderRequest& request) final;
    virtual bool pushPrepare(const PrepareRequest& request) final;
    virtual void pushFinalize() final;

protected:
    // ========================================
    // 派生クラスでオーバーライドするフック
    // ========================================

    virtual RenderResponse onPullProcess(const RenderRequest& request);
    virtual bool onPullPrepare(const PrepareRequest& request);
    virtual void onPullFinalize();

    virtual void onPushProcess(RenderResponse&& input, const RenderRequest& request);
    virtual bool onPushPrepare(const PrepareRequest& request);
    virtual void onPushFinalize();
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
    void setVirtualScreen(int width, int height);

    // pivot設定（スクリーン座標でワールド原点の表示位置を指定）
    void setPivot(float x, float y);
    void setPivotCenter();  // 中央に設定

    // pivotアクセサ
    std::pair<float, float> getPivot() const;

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

**注意**: タイル分割時、Renderer下流に配置したブラーフィルタはタイル境界で正しく動作しません。

## カスタム拡張

### 入出力サイズが異なるノード

`pullProcess()` をオーバーライドして入力要求を変更できます：

```cpp
class BlurNode : public Node {
    RenderResponse pullProcess(const RenderRequest& request) override {
        // カーネルサイズ分だけ拡大したrequestで上流を評価
        RenderRequest expandedReq = request.expand(radius_);
        RenderResponse input = upstreamNode(0)->pullProcess(expandedReq);
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
