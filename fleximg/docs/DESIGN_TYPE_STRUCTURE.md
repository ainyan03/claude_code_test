# 型構造設計

fleximg における画像データ関連の型構造について説明します。

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
│  RenderResult（パイプライン評価結果）                      │
│  - ImageBuffer buffer                                   │
│  - Point origin（バッファ内での基準点位置、int_fixed8）    │
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

    // ブレンド操作
    void blendFirst(ViewPort& dst, int dstX, int dstY,
                    const ViewPort& src, int srcX, int srcY,
                    int width, int height);

    void blendOnto(ViewPort& dst, int dstX, int dstY,
                   const ViewPort& src, int srcX, int srcY,
                   int width, int height);

    // 矩形コピー・クリア
    void copy(ViewPort& dst, int dstX, int dstY,
              const ViewPort& src, int srcX, int srcY,
              int width, int height);
    void clear(ViewPort& dst, int x, int y, int width, int height);
}
```

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

## RenderResult（パイプライン評価結果）

パイプライン処理における評価結果と座標情報を保持します。

```cpp
struct RenderResult {
    ImageBuffer buffer;
    Point origin;  // バッファ内での基準点位置（int_fixed8）

    // コンストラクタ
    RenderResult();
    RenderResult(ImageBuffer&& buf, Point org);
    RenderResult(ImageBuffer&& buf, float ox, float oy);  // マイグレーション用

    // ムーブのみ（コピー禁止）
    RenderResult(RenderResult&&) = default;
    RenderResult& operator=(RenderResult&&) = default;

    // ユーティリティ
    bool isValid() const;
    ViewPort view();
    ViewPort view() const;
};
```

**責務**: パイプライン処理結果と座標情報の保持

### origin の意味

`origin` はバッファ内での基準点のピクセル位置を表します。RenderRequest と同じ意味です。

| 画像サイズ | 基準点位置 | origin.x | origin.y |
|-----------|-----------|----------|----------|
| 100x100 | 左上 (0, 0) | 0 | 0 |
| 100x100 | 中央 | 50 | 50 |
| 100x100 | 右下 | 100 | 100 |

## 使用例

### 基本的なパイプライン処理

```cpp
// ノードから結果を取得
RenderResult result = node->pullProcess(request);

// 両方の origin は同じ意味（バッファ内基準点位置）なので直接比較可能
// オフセット = request の基準点位置 - result の基準点位置
int offsetX = static_cast<int>(request.origin.x - result.origin.x);
int offsetY = static_cast<int>(request.origin.y - result.origin.y);

// キャンバスにブレンド
ViewPort canvas = canvasBuffer.view();
view_ops::blendOnto(canvas, offsetX, offsetY,
                    result.view(), 0, 0,
                    result.buffer.width(), result.buffer.height());
```

### blendFirst vs blendOnto

| 関数 | 用途 |
|-----|------|
| blendFirst | 透明キャンバスへの最初の描画（memcpy最適化）|
| blendOnto | 2枚目以降の合成（ブレンド計算）|

## 設計の利点

1. **責務の明確化**: ViewPort＝ビュー、ImageBuffer＝所有、RenderResult＝処理結果
2. **軽量ビュー**: ViewPort だけ渡せば済む場面で効率的
3. **スライシング防止**: コンポジションにより安全
4. **座標計算の局所化**: パイプライン処理側に集約

## 注意事項

- blendFirst/blendOnto は RGBA16_Premultiplied 専用
- ViewPort はメモリを所有しないため、元の ImageBuffer より長く生存してはならない

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/core/types.h` | 固定小数点型、数学型、AffineMatrix |
| `src/fleximg/core/common.h` | NAMESPACE定義、バージョン |
| `src/fleximg/core/memory/allocator.h` | IAllocator, DefaultAllocator |
| `src/fleximg/image/viewport.h` | ViewPort, view_ops |
| `src/fleximg/image/image_buffer.h` | ImageBuffer |
| `src/fleximg/image/render_types.h` | RenderResult, RenderRequest |
