# ViewPort構造改訂設計 ✅ 完了

## 概要

ViewPortを本来の責務（画像データへのビュー）に限定し、座標情報をパイプライン処理側に移動する設計。

> **ステータス**: 2026-01-08 実装完了
> - ImageBuffer, NewViewPort, EvalResult の3型に分離
> - オペレーターAPIを新構造に移行
> - 旧ViewPort (viewport.h/cpp) を削除
> - 60テストがパス

---

## ✅ NewViewPort → ViewPort リネーム完了

2026-01-08 に NewViewPort → ViewPort のリネームを完了しました。

変更内容:
- `viewport_new.h/cpp` → `viewport.h/cpp` にファイル名変更
- `struct NewViewPort` → `struct ViewPort` にリネーム
- 全ソースファイルでの参照を更新
- Makefile/build.sh のファイル名を更新
- 60テスト全てパス

---

## 現状の問題（リファクタリング前の状態記録）

現在のViewPortは以下の責務を混在させている：

```cpp
struct ViewPort {
    // 本来のビューポート責務
    void* data;
    PixelFormatID formatID;
    size_t stride;
    int width, height;

    // メモリ管理（所有権管理）
    size_t capacity;
    ImageAllocator* allocator;
    bool ownsData;

    // サブビュー機能
    int offsetX, offsetY;
    ViewPort* parent;

    // パイプライン座標（処理コンテキスト）
    float srcOriginX, srcOriginY;
};
```

**問題点**:
- 「ビューポート」という名前に対して責務が広すぎる
- パイプライン処理専用の座標情報がデータ構造に混入
- 軽量なビューだけが必要な場面でも重い構造を使用

## 新しい設計

### 1. 型の分離

```
┌─────────────────────────────────────────────────────────┐
│  ViewPort（純粋ビュー）                                   │
│  - data, formatID, stride, width, height                │
│  - blendFirst(), blendOnto()                            │
└─────────────────────────────────────────────────────────┘
            ▲
            │ 包含
┌─────────────────────────────────────────────────────────┐
│  ImageBuffer（メモリ所有）                                │
│  - ViewPort view                                        │
│  - capacity, allocator, ownsData                        │
│  - サブビュー機能                                         │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│  EvalResult（パイプライン結果）                           │
│  - ImageBuffer buffer (または ViewPort view)             │
│  - Point2f origin                                       │
└─────────────────────────────────────────────────────────┘
```

### 2. ViewPort（純粋ビュー）

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
};
```

**責務**: 画像データへの読み書きアクセスのみ

### 3. ImageBuffer（メモリ所有画像）

```cpp
struct ImageBuffer {
    ViewPort view;          // ビューとしてアクセス可能
    size_t capacity;
    ImageAllocator* allocator;
    bool ownsData;

    // コンストラクタ/デストラクタ（RAII）
    ImageBuffer(int w, int h, PixelFormatID fmt, ImageAllocator* alloc = nullptr);
    ~ImageBuffer();

    // コピー/ムーブ
    ImageBuffer(const ImageBuffer& other);
    ImageBuffer(ImageBuffer&& other) noexcept;

    // サブビュー作成
    ViewPort createSubView(int x, int y, int w, int h);

    // ViewPortへの暗黙変換（利便性）
    operator ViewPort&() { return view; }
    operator const ViewPort&() const { return view; }

    // Image変換（互換性）
    static ImageBuffer fromImage(const Image& img);
    Image toImage() const;
};
```

**責務**: メモリの確保・解放・所有権管理

### 4. 座標構造体

```cpp
struct Point2f {
    float x = 0;
    float y = 0;

    Point2f() = default;
    Point2f(float x_, float y_) : x(x_), y(y_) {}

    Point2f operator+(const Point2f& o) const { return {x + o.x, y + o.y}; }
    Point2f operator-(const Point2f& o) const { return {x - o.x, y - o.y}; }
};
```

### 5. EvalResult（パイプライン評価結果）

```cpp
struct EvalResult {
    ImageBuffer buffer;
    Point2f origin;         // 基準点からの相対座標

    // オフセット計算ヘルパー
    std::pair<int, int> offsetFrom(const Point2f& canvasOrigin) const {
        return {
            static_cast<int>(origin.x - canvasOrigin.x),
            static_cast<int>(origin.y - canvasOrigin.y)
        };
    }
};
```

**責務**: パイプライン処理における評価結果と座標情報の保持

## blendOnto/blendFirst の設計

### API

```cpp
// ViewPortのメンバ関数
void ViewPort::blendFirst(const ViewPort& src, int offsetX, int offsetY);
void ViewPort::blendOnto(const ViewPort& src, int offsetX, int offsetY);
```

### 使用例

```cpp
// パイプライン処理側
EvalResult result = evaluateNode(...);
Point2f canvasOrigin = {-request.originX, -request.originY};

auto [offsetX, offsetY] = result.offsetFrom(canvasOrigin);
canvas.blendOnto(result.buffer.view, offsetX, offsetY);
```

### 実装（ViewPort内）

```cpp
void ViewPort::blendOnto(const ViewPort& src, int offsetX, int offsetY) {
    // クリッピング範囲計算
    int yStart = std::max(0, -offsetY);
    int yEnd = std::min(src.height, height - offsetY);
    int xStart = std::max(0, -offsetX);
    int xEnd = std::min(src.width, width - offsetX);

    if (yStart >= yEnd || xStart >= xEnd) return;

    // ブレンド処理（RGBA16_Premultiplied前提）
    for (int y = yStart; y < yEnd; y++) {
        const uint16_t* srcRow = src.getPixelPtr<uint16_t>(0, y);
        uint16_t* dstRow = getPixelPtr<uint16_t>(0, y + offsetY);
        // ... ブレンド処理
    }
}
```

## 移行計画

### Phase 1: 構造体の追加
- Point2f を common.h に追加
- ImageBuffer を新規作成（現ViewPortをベースに）
- ViewPort を純粋ビューとして再定義

### Phase 2: blendFirst/blendOnto の移動
- CompositeOperator から ViewPort へ移動
- オフセット引数を受け取る形式に変更
- 呼び出し側で座標計算を行うよう修正

### Phase 3: パイプライン座標の移動
- EvalResult 構造体の導入
- evaluation_node.cpp での srcOriginX/Y を EvalResult.origin に移行
- node_graph.cpp の評価結果を EvalResult で返す

### Phase 4: 旧API削除
- ViewPort から srcOriginX/Y を削除
- 旧 CompositeOperator の blendFirst/blendOnto を削除

## ファイル構成（変更後）

```
src/fleximg/
├── common.h              # Point2f追加
├── viewport.h            # 純粋ViewPort
├── image_buffer.h        # ImageBuffer（新規）
├── image_buffer.cpp      # ImageBuffer実装（新規）
├── eval_result.h         # EvalResult（新規）
├── operators.h           # CompositeOperatorからblend系削除
├── operators.cpp
├── evaluation_node.h
├── evaluation_node.cpp   # EvalResult使用
└── node_graph.h/cpp      # EvalResult使用
```

## 利点

1. **責務の明確化**: ViewPort＝ビュー、ImageBuffer＝所有、EvalResult＝処理結果
2. **軽量ビュー**: ViewPortだけ渡せば済む場面で効率的
3. **座標計算の局所化**: パイプライン処理側に集約
4. **API明確化**: blendOntoのオフセットが引数で明示的
5. **テスト容易性**: 座標なしでブレンド処理を単体テスト可能

## 注意事項

- blendFirst/blendOnto は現状 RGBA16_Premultiplied 専用
- 将来的にフォーマット汎用化する場合は別途検討
- ImageBuffer → ViewPort の暗黙変換で既存コードへの影響を最小化
