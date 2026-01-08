# 型構造設計

fleximg における画像データ関連の型構造について説明します。

## 型の分離

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
│  - ViewPort を内包（view() で取得）                       │
│  - capacity, allocator, ownsData                        │
│  - RAII によるメモリ管理                                  │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  EvalResult（パイプライン評価結果）                        │
│  - ImageBuffer buffer                                   │
│  - Point2f origin（基準点からの相対座標）                  │
└─────────────────────────────────────────────────────────┘
```

## ViewPort（純粋ビュー）

画像データへの読み書きアクセスのみを担当する軽量な構造体です。

```cpp
struct ViewPort {
    void* data;
    PixelFormatID formatID;
    size_t stride;
    int width, height;

    // ピクセルアクセス
    void* getPixelAddress(int x, int y);
    template<typename T> T* getPixelPtr(int x, int y);

    // ブレンド操作（オフセット指定）
    void blendFirst(const ViewPort& src, int offsetX, int offsetY);
    void blendOnto(const ViewPort& src, int offsetX, int offsetY);

    // フォーマット情報
    const PixelFormatDescriptor& getFormatDescriptor() const;
    size_t getBytesPerPixel() const;

    // サブビュー作成
    ViewPort subView(int x, int y, int w, int h) const;

    // フォーマット変換
    ImageBuffer toImageBuffer(PixelFormatID targetFormat) const;
};
```

**責務**: 画像データへの読み書きアクセスのみ

## ImageBuffer（メモリ所有画像）

メモリの確保・解放を担当する RAII クラスです。

```cpp
class ImageBuffer {
public:
    // コンストラクタ/デストラクタ（RAII）
    ImageBuffer(int w, int h, PixelFormatID fmt, ImageAllocator* alloc = nullptr);
    ~ImageBuffer();

    // コピー/ムーブ
    ImageBuffer(const ImageBuffer& other);
    ImageBuffer(ImageBuffer&& other) noexcept;

    // ViewPort としてアクセス
    ViewPort view();
    ViewPort view() const;
    ViewPort subView(int x, int y, int w, int h);

    // Image変換（API境界用）
    static ImageBuffer fromImage(const Image& img);
    Image toImage() const;

    // メンバ
    PixelFormatID formatID;
    int width, height;
};
```

**責務**: メモリの確保・解放・所有権管理

## EvalResult（パイプライン評価結果）

パイプライン処理における評価結果と座標情報を保持します。

```cpp
struct EvalResult {
    ImageBuffer buffer;
    Point2f origin;  // 基準点からの相対座標

    // 有効性チェック
    bool isValid() const;

    // コンストラクタ
    EvalResult();
    EvalResult(ImageBuffer buf, Point2f org);
};
```

**責務**: パイプライン処理結果と座標情報の保持

## Point2f（座標）

```cpp
struct Point2f {
    float x = 0;
    float y = 0;

    Point2f() = default;
    Point2f(float x_, float y_) : x(x_), y(y_) {}

    Point2f operator+(const Point2f& o) const;
    Point2f operator-(const Point2f& o) const;
};
```

## blendOnto/blendFirst の使い方

```cpp
// パイプライン処理側での使用例
EvalResult result = evaluateNode(...);

// オフセット計算（基準点を合わせる）
int offsetX = static_cast<int>(request.originX + result.origin.x);
int offsetY = static_cast<int>(request.originY + result.origin.y);

// キャンバスにブレンド
ViewPort canvasView = canvas.view();
canvasView.blendOnto(result.buffer.view(), offsetX, offsetY);
```

### blendFirst vs blendOnto

| メソッド | 用途 |
|---------|------|
| blendFirst | 透明キャンバスへの最初の描画（memcpy最適化）|
| blendOnto | 2枚目以降の合成（ブレンド計算）|

## 設計の利点

1. **責務の明確化**: ViewPort＝ビュー、ImageBuffer＝所有、EvalResult＝処理結果
2. **軽量ビュー**: ViewPort だけ渡せば済む場面で効率的
3. **座標計算の局所化**: パイプライン処理側に集約
4. **API明確化**: blendOnto のオフセットが引数で明示的
5. **テスト容易性**: 座標なしでブレンド処理を単体テスト可能

## 注意事項

- blendFirst/blendOnto は RGBA16_Premultiplied 専用
- ViewPort はメモリを所有しないため、元の ImageBuffer より長く生存してはならない

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/viewport.h/cpp` | ViewPort |
| `src/fleximg/image_buffer.h/cpp` | ImageBuffer |
| `src/fleximg/eval_result.h` | EvalResult |
| `src/fleximg/common.h` | Point2f |
