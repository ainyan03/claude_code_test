# 型構造設計

fleximg における画像データ関連の型構造について説明します。

## 型の継承関係

```
┌─────────────────────────────────────────────────────────┐
│  ViewPort（純粋ビュー・基底クラス）                        │
│  - data, formatID, stride, width, height                │
│  - getPixelAddress(), getPixelPtr<T>()                  │
│  - blendFirst(), blendOnto()                            │
│  - 所有権なし、軽量                                       │
└─────────────────────────────────────────────────────────┘
            ▲
            │ 継承
┌─────────────────────────────────────────────────────────┐
│  ImageBuffer（メモリ所有・派生クラス）                     │
│  - ViewPort のフィールド・メソッドを継承                   │
│  - capacity, allocator（追加メンバ）                      │
│  - RAII によるメモリ管理                                  │
│  - ViewPort& を受け取る関数に直接渡せる                   │
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

ViewPort を継承し、メモリの確保・解放を担当する RAII クラスです。

```cpp
struct ImageBuffer : public ViewPort {
    // ImageBuffer 固有メンバ
    size_t capacity;          // 確保済みバイト数
    ImageAllocator* allocator; // メモリアロケータ

    // コンストラクタ/デストラクタ（RAII）
    ImageBuffer(int w, int h, PixelFormatID fmt, ImageAllocator* alloc = nullptr);
    ~ImageBuffer();

    // コピー/ムーブ
    ImageBuffer(const ImageBuffer& other);
    ImageBuffer(ImageBuffer&& other) noexcept;

    // ViewPort として取得（スライシング）
    ViewPort view();
    ViewPort view() const;

    // ImageBuffer 固有ユーティリティ
    size_t getTotalBytes() const;
    ImageBuffer convertTo(PixelFormatID targetFormat) const;

    // 注: ピクセルアクセス、フォーマット情報、subView 等は ViewPort から継承
};
```

**責務**: メモリの確保・解放・所有権管理

**継承による利点**:
- ViewPort& を受け取る関数に ImageBuffer を直接渡せる
- 重複コードを削減（フィールド5個、メソッド7個分）

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
- ImageBuffer を ViewPort としてコピーした場合も同様（スライシング後の ViewPort は元の ImageBuffer 破棄後は dangling）

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/viewport.h/cpp` | ViewPort |
| `src/fleximg/image_buffer.h/cpp` | ImageBuffer |
| `src/fleximg/eval_result.h` | EvalResult |
| `src/fleximg/common.h` | Point2f |
