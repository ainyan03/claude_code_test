# 型構造設計

fleximg における画像データ関連の型構造について説明します。

## 座標系設計の背景

### なぜ「基準点相対座標」なのか

fleximg では、すべての座標を「基準点（origin）からの相対位置」で表現します。この設計には以下の理由があります。

#### 1. 絶対座標の問題

絶対座標（画面左上を0,0とする方式）では、画像を移動するたびに全ピクセルの座標を再計算する必要があります。

```
【絶対座標の問題】
画像Aを(100, 50)に配置 → 画像Bを(200, 100)に配置
↓ 画像Aを(150, 75)に移動
全ての座標計算をやり直す必要がある
```

#### 2. 基準点相対座標の利点

基準点を基準にすれば、各ノードは「自分の基準点からの相対位置」だけを管理すればよく、上流/下流の配置に依存しません。

```
【基準点相対座標】
各ノードは「基準点から何ピクセル離れているか」だけを記録
↓
配置変更 = RendererNode の設定変更のみ
各ノードの処理ロジックは変更不要
```

#### 3. アフィン変換との相性

回転・拡大は「どこを中心に」行うかが重要です。基準点を中心とすることで、変換の意図が明確になります。

```
基準点を中央に設定 → 中央を軸に回転
基準点を左上に設定 → 左上を軸に回転
```

### ViewPort / ImageBuffer / RenderResponse の使い分け

| 型 | 座標情報 | 用途 |
|----|---------|------|
| ViewPort | なし | 純粋なピクセルデータへのアクセス |
| ImageBuffer | なし | メモリ管理（確保・解放） |
| RenderResponse | origin | パイプライン処理結果 + 座標情報 |

**RenderResponse だけが座標情報（origin）を持ちます。** これにより、画像データと座標情報の責務が明確に分離されます。

---

## 型の関係

```
┌─────────────────────────────────────────────────────────┐
│  ViewPort（純粋ビュー・POD）                              │
│  - data, formatID, stride, width, height                │
│  - 所有権なし、軽量                                       │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  ImageBuffer（メモリ所有・コンポジション）                  │
│  - ViewPort view_（内部メンバ）                          │
│  - view() で ViewPort を取得                            │
│  - RAII によるメモリ管理                                  │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  RenderResponse（パイプライン評価結果）                      │
│  - ImageBuffer buffer                                   │
│  - Point origin（バッファ内での基準点位置、int_fixed Q16.16）│
└─────────────────────────────────────────────────────────┘
```

## ViewPort（純粋ビュー）

画像データへの軽量なビューです。メモリを所有しません。

```cpp
struct ViewPort {
    void* data = nullptr;
    PixelFormatID formatID = PixelFormatIDs::RGBA8_Straight;
    int32_t stride = 0;   // 負値でY軸反転対応
    int16_t width = 0;
    int16_t height = 0;

    // 有効判定
    bool isValid() const;

    // ピクセルアクセス
    void* pixelAt(int x, int y);
    const void* pixelAt(int x, int y) const;

    // バイト情報
    size_t bytesPerPixel() const;
    uint32_t rowBytes() const;
};
```

**責務**: 画像データへの読み書きアクセスのみ

### view_ops 名前空間

ViewPort への操作はフリー関数として提供されます。

```cpp
namespace view_ops {
    // サブビュー作成
    ViewPort subView(const ViewPort& v, int x, int y, int w, int h);

    // 矩形コピー・クリア
    void copy(ViewPort& dst, int dstX, int dstY,
              const ViewPort& src, int srcX, int srcY,
              int width, int height);
    void clear(ViewPort& dst, int x, int y, int width, int height);
}
```

合成操作は `canvas_utils` 名前空間で提供されます（`operations/canvas_utils.h`）。

## ImageBuffer（メモリ所有画像）

画像データを所有するクラスです。**コンポジション**により ViewPort を内部に保持します。

```cpp
class ImageBuffer {
public:
    // コンストラクタ
    ImageBuffer();  // 空の画像
    ImageBuffer(int w, int h, PixelFormatID fmt = PixelFormatIDs::RGBA8_Straight,
                InitPolicy init = InitPolicy::Zero,
                core::memory::IAllocator* alloc = &core::memory::DefaultAllocator::instance());

    // コピー/ムーブ
    ImageBuffer(const ImageBuffer& other);      // ディープコピー
    ImageBuffer(ImageBuffer&& other) noexcept;  // ムーブ

    // ビュー取得
    ViewPort view();              // 値で返す（安全）
    ViewPort view() const;
    ViewPort& viewRef();          // 参照で返す（効率重視）
    const ViewPort& viewRef() const;
    ViewPort subView(int x, int y, int w, int h) const;

    // アクセサ
    bool isValid() const;
    bool ownsMemory() const;      // メモリ所有の有無
    int16_t width() const;
    int16_t height() const;
    int32_t stride() const;
    PixelFormatID formatID() const;
    uint32_t totalBytes() const;
    void* data();
    const void* data() const;

    // フォーマット変換（右辺値参照版）
    ImageBuffer toFormat(PixelFormatID target, FormatConversion mode = FormatConversion::CopyIfNeeded) &&;

private:
    ViewPort view_;                       // コンポジション
    size_t capacity_;
    core::memory::IAllocator* allocator_; // メモリアロケータ
    InitPolicy initPolicy_;
};
```

**責務**: メモリの確保・解放・所有権管理

### コンポジション設計の利点

- **スライシング防止**: 継承ではないため、値渡しでデータが失われない
- **明確な所有権**: ImageBuffer がメモリを所有、ViewPort は参照のみ
- **安全なAPI**: `view()` で明示的にビューを取得

### toFormat() の使い方

同じフォーマットならムーブ、異なるなら変換します。

```cpp
// 入力を RGBA8_Straight に変換して処理
ImageBuffer working = std::move(input.buffer).toFormat(PixelFormatIDs::RGBA8_Straight);
```

## RenderResponse（パイプライン評価結果）

パイプライン処理における評価結果と座標情報を保持します。

```cpp
struct RenderResponse {
    ImageBuffer buffer;
    Point origin;  // バッファ内での基準点位置（int_fixed Q16.16）

    // コンストラクタ
    RenderResponse();
    RenderResponse(ImageBuffer&& buf, Point org);
    RenderResponse(ImageBuffer&& buf, float ox, float oy);  // マイグレーション用

    // ムーブのみ（コピー禁止）
    RenderResponse(RenderResponse&&) = default;
    RenderResponse& operator=(RenderResponse&&) = default;

    // ユーティリティ
    bool isValid() const;
    ViewPort view();
    ViewPort view() const;
};
```

**責務**: パイプライン処理結果と座標情報の保持

### origin の意味

`origin` は**バッファ左上（[0,0]）のワールド座標**を表します。RenderRequest と RenderResponse で同じ意味です。

```
【ワールド座標系】
ワールド原点 (0,0) を基準として、各バッファの左上がどこにあるかを表す

例: 100x100 の画像を中央配置する場合
- バッファ左上のワールド座標 = (-50, -50)
- origin = {-50, -50}
```

| 配置 | バッファ左上のワールド座標 | origin.x | origin.y |
|------|--------------------------|----------|----------|
| 中央配置 (100x100画像) | (-50, -50) | -50 | -50 |
| 左上配置 | (0, 0) | 0 | 0 |
| 右下にオフセット | (100, 50) | 100 | 50 |

### offset 計算

2つのバッファ間のピクセルオフセットは、origin の差分で計算します:

```cpp
// src を dst に配置する際のオフセット
int offsetX = from_fixed(src.origin.x - dst.origin.x);
int offsetY = from_fixed(src.origin.y - dst.origin.y);
// → src のピクセルを dst の (offsetX, offsetY) に配置
```

## 使用例

### 基本的なパイプライン処理

```cpp
// ノードから結果を取得
RenderResponse result = node->pullProcess(request);

// 両方の origin は同じ意味（バッファ内基準点位置）なので直接比較可能
// オフセット = request の基準点位置 - result の基準点位置
int offsetX = static_cast<int>(request.origin.x - result.origin.x);
int offsetY = static_cast<int>(request.origin.y - result.origin.y);

// キャンバスに配置（canvas_utils名前空間を使用）
ViewPort canvas = canvasBuffer.view();
canvas_utils::placeFirst(canvas, request.origin.x, request.origin.y,
                         result.view(), result.origin.x, result.origin.y);
```

### canvas_utils の合成関数

| 関数 | 用途 |
|-----|------|
| canvas_utils::placeFirst | 透明キャンバスへの最初の描画（変換コピー）|
| canvas_utils::placeUnder | under合成（dst不透明ならスキップ）|

> **Note**: `blend::onto` および `canvas_utils::placeOnto` は廃止されました。
> over合成はunder合成に統一され、CompositeNode等では `placeFirst` + `placeUnder` 方式を使用します。

これらは固定小数点の基準点座標（`int_fixed` Q16.16）を使用し、PixelFormatDescriptor の変換関数（toStraight, blendUnderStraight）を利用します。

## 設計の利点

1. **責務の明確化**: ViewPort＝ビュー、ImageBuffer＝所有、RenderResponse＝処理結果
2. **軽量ビュー**: ViewPort だけ渡せば済む場面で効率的
3. **スライシング防止**: コンポジションにより安全
4. **座標計算の局所化**: パイプライン処理側に集約

## フォーマット変換

`convertFormat()` 関数でピクセルフォーマット間の変換を行います。

```cpp
// 同じフォーマット: 単純コピー
// 直接変換が定義されている場合: 最適化パス
// その他: RGBA8_Straight 経由で変換
convertFormat(src, srcFormat, dst, dstFormat, pixelCount);
```

ImageBuffer の `toFormat()` メソッドでも変換できます。

```cpp
ImageBuffer working = std::move(input).toFormat(PixelFormatIDs::RGBA8_Straight);
```

## 注意事項

- ViewPort はメモリを所有しないため、元の ImageBuffer より長く生存してはならない

## AffineCapability Mixin

アフィン変換機能を提供する Mixin クラスです。Node とは独立して設計されており、多重継承で使用します。

```cpp
class AffineCapability {
public:
    // 行列操作
    void setMatrix(const AffineMatrix& m);
    const AffineMatrix& matrix() const;

    // 便利なセッター（担当要素のみ変更）
    void setRotation(float radians);           // a,b,c,d のみ
    void setScale(float sx, float sy);         // a,b,c,d のみ
    void setTranslation(float tx, float ty);   // tx,ty のみ
    void setRotationScale(float radians, float sx, float sy);  // a,b,c,d のみ

    // ユーティリティ
    bool hasLocalTransform() const;  // 単位行列でないか判定

protected:
    AffineMatrix localMatrix_;  // デフォルトは単位行列
};
```

### 適用ノード

| ノード | 用途 |
|--------|------|
| AffineNode | パススルー変換（既存機能） |
| SourceNode | 入力画像の個別変換 |
| SinkNode | 出力先での変換 |
| CompositeNode | 合成結果全体の変換（全上流に伝播） |
| DistributorNode | 分配先全体への変換（全下流に伝播） |

### 行列合成順序

AffineNode 直列接続と同じ解釈順序を維持:

```cpp
// Pull型（SourceNode, CompositeNode）
combinedMatrix = request.affineMatrix * localMatrix_;

// Push型（SinkNode, DistributorNode）
combinedMatrix = request.pushAffineMatrix * localMatrix_;
```

「自身の変換を先に適用し、その後パイプライン経由の変換を適用」という解釈です。

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/core/types.h` | 固定小数点型、数学型、AffineMatrix |
| `src/fleximg/core/affine_capability.h` | AffineCapability Mixin |
| `src/fleximg/core/common.h` | NAMESPACE定義、バージョン |
| `src/fleximg/core/memory/allocator.h` | IAllocator, DefaultAllocator |
| `src/fleximg/image/pixel_format.h` | PixelFormatID, PixelFormatDescriptor, convertFormat() |
| `src/fleximg/image/viewport.h` | ViewPort, view_ops |
| `src/fleximg/image/image_buffer.h` | ImageBuffer |
| `src/fleximg/image/render_types.h` | RenderResponse, RenderRequest |
