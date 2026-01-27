# インデックスカラー（パレット）フォーマット（Indexed Color Formats）

## 概要

シングルチャンネルピクセルデータをパレットインデックスとして使用するインデックスカラー（パレット）フォーマットを追加する。

- **Index8**: 8bit パレットインデックス（256色、1byte/pixel）
- **Index4**: 4bit パレットインデックス（16色、0.5byte/pixel）
- **Index2**: 2bit パレットインデックス（4色、0.25byte/pixel）
- **Index1**: 1bit パレットインデックス（2色、0.125byte/pixel）

## 動機

### 必要性

1. **GIF/PNG等のパレット画像対応**
   - GIF: 最大256色（8bit）パレット
   - PNG: 1/2/4/8bit パレットモード対応
   - レトロゲームアセット（NES, Game Boy等）

2. **メモリ効率**
   - 256色で十分な画像の場合、RGBA8比で75%削減
   ```
   512×512 画像（256色）:
   - RGBA8: 1,048,576 bytes (4byte/pixel)
   - Index8:   262,144 bytes (1byte/pixel) + パレット 1,024 bytes
   - 削減率: 75%
   ```

3. **減色処理パイプライン**
   - アーティスティックな減色効果（レトロ風、ピクセルアート風）
   - ディザリング処理との組み合わせ
   - 色数制限のあるハードウェアへの出力

### Gray8との関係

同じシングルチャンネル（1byte）だが、**意味と用途が異なる**：

| 項目 | Gray8 | Index8 |
|------|-------|--------|
| メモリレイアウト | 1byte/pixel | 1byte/pixel |
| 値の意味 | 輝度値（0-255） | パレットインデックス（0-255） |
| 色の決定 | 値そのものがグレー階調 | パレットを参照してRGBA決定 |
| パレット依存 | なし | あり（必須） |
| 変換方式 | 輝度計算 | 色量子化 |
| 用途 | グレースケール画像、アルファマスク | 減色画像、GIF/PNG、レトロ風 |

### 現状の対応状況

`PixelFormatDescriptor` はすでにインデックスカラーのインフラを持つ：

```cpp
struct PixelFormatDescriptor {
    // ...
    bool isIndexed;                // パレット形式かどうか
    uint16_t maxPaletteSize;       // 最大パレットサイズ

    // パレット付き変換関数（パレットはRGBA8_Straight形式）
    using ToStandardIndexedFunc = void(*)(const void* src, uint8_t* dst,
                                          int pixelCount, const uint8_t* palette);
    using FromStandardIndexedFunc = void(*)(const uint8_t* src, void* dst,
                                            int pixelCount, const uint8_t* palette);

    ToStandardIndexedFunc toStandardIndexed;
    FromStandardIndexedFunc fromStandardIndexed;
};
```

しかし、**実装例が1つもない**（全フォーマットで `isIndexed = false`）。

## 提案

### Index8フォーマット定義

#### 基本仕様

```cpp
// pixel_format.cpp

namespace BuiltinFormats {

const PixelFormatDescriptor Index8 = {
    "Index8",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    { ChannelDescriptor(8, 0),   // Index値（0-255）
      ChannelDescriptor(0, 0),   // No G
      ChannelDescriptor(0, 0),   // No B
      ChannelDescriptor(0, 0) }, // No A
    false,  // hasAlpha (パレットがアルファを持つ可能性はある)
    true,   // isIndexed ← 重要
    256,    // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    nullptr,  // toStandard (ダイレクトカラー用、使わない)
    nullptr,  // fromStandard
    index8_toStandardIndexed,   // パレット付き変換
    index8_fromStandardIndexed  // パレット付き変換
};

} // namespace BuiltinFormats
```

#### 変換関数実装

```cpp
// Index8 → RGBA8_Straight（パレット参照）
static void index8_toStandardIndexed(const void* src, uint8_t* dst,
                                      int pixelCount, const uint8_t* palette) {
    const uint8_t* s = static_cast<const uint8_t*>(src);

    if (!palette) {
        // パレットなし: グレースケールとして扱う（フォールバック）
        for (int i = 0; i < pixelCount; i++) {
            uint8_t index = s[i];
            dst[i*4 + 0] = index;
            dst[i*4 + 1] = index;
            dst[i*4 + 2] = index;
            dst[i*4 + 3] = 255;
        }
        return;
    }

    // パレット参照（palette: RGBA8_Straight形式）
    for (int i = 0; i < pixelCount; i++) {
        uint8_t index = s[i];
        const uint8_t* color = &palette[index * 4];

        // RGBA8 → RGBA8（単純コピー）
        dst[i*4 + 0] = color[0];  // R
        dst[i*4 + 1] = color[1];  // G
        dst[i*4 + 2] = color[2];  // B
        dst[i*4 + 3] = color[3];  // A
    }
}

// RGBA8_Straight → Index8（色量子化）
static void index8_fromStandardIndexed(const uint8_t* src, void* dst,
                                        int pixelCount, const uint8_t* palette) {
    uint8_t* d = static_cast<uint8_t*>(dst);

    if (!palette) {
        // パレットなし: グレースケール変換（フォールバック）
        for (int i = 0; i < pixelCount; i++) {
            uint16_t r = src[i*4 + 0];
            uint16_t g = src[i*4 + 1];
            uint16_t b = src[i*4 + 2];
            d[i] = static_cast<uint8_t>((77*r + 150*g + 29*b) >> 8);
        }
        return;
    }

    // 色量子化: 最近傍色をパレットから探索
    for (int i = 0; i < pixelCount; i++) {
        uint8_t r = src[i*4 + 0];
        uint8_t g = src[i*4 + 1];
        uint8_t b = src[i*4 + 2];

        // 最近傍探索（ユークリッド距離）
        int bestIndex = 0;
        int minDistance = INT_MAX;

        for (int p = 0; p < 256; p++) {
            const uint8_t* color = &palette[p * 4];
            int pr = color[0];
            int pg = color[1];
            int pb = color[2];

            int dr = r - pr;
            int dg = g - pg;
            int db = b - pb;
            int distance = dr*dr + dg*dg + db*db;

            if (distance < minDistance) {
                minDistance = distance;
                bestIndex = p;
            }
        }

        d[i] = bestIndex;
    }
}
```

### パレット管理の設計

#### 課題

現状の `convertFormat()` はパレットを引数で受け取るが、**パレットの保持場所が未定義**：

```cpp
inline void convertFormat(const void* src, PixelFormatID srcFormat,
                          void* dst, PixelFormatID dstFormat,
                          int pixelCount,
                          const uint8_t* srcPalette = nullptr,  // どこから来る？
                          const uint8_t* dstPalette = nullptr);
```

#### 提案：ImageBufferの拡張

##### 案A: ImageBufferにパレット情報を追加

```cpp
// image_buffer.h

class ImageBuffer {
public:
    // ... 既存フィールド ...

    // パレット情報（オプション）
    void setPalette(const uint8_t* palette, size_t colorCount);
    const uint8_t* getPalette() const;
    size_t getPaletteSize() const;
    bool hasPalette() const;

private:
    PixelFormatID format_;
    // ... 既存フィールド ...

    // パレット（RGBA8_Straight形式、最大256色）
    std::vector<uint8_t> palette_;  // 新規追加
};
```

実装例：

```cpp
void ImageBuffer::setPalette(const uint8_t* palette, size_t colorCount) {
    if (!format_->isIndexed) {
        // 警告: インデックスカラーではないフォーマット
        return;
    }

    size_t maxColors = format_->maxPaletteSize;
    if (colorCount > maxColors) {
        colorCount = maxColors;
    }

    palette_.resize(colorCount * 4);  // RGBA8
    std::memcpy(palette_.data(), palette, colorCount * 4 * sizeof(uint8_t));
}

const uint8_t* ImageBuffer::getPalette() const {
    return palette_.empty() ? nullptr : palette_.data();
}
```

フォーマット変換時：

```cpp
// 変換時にImageBufferのパレットを自動的に使用
RenderResponse processIndexedImage(ImageBuffer& buffer) {
    ImageBuffer rgba8Buffer = ...;

    // ImageBufferがパレットを保持
    convertFormat(buffer.pixels(), buffer.format(),
                  rgba8Buffer.pixels(), rgba8Buffer.format(),
                  buffer.width() * buffer.height(),
                  buffer.getPalette(),  // 自動的にパレットを渡す
                  nullptr);
}
```

**メリット**:
- ✅ パレットと画像データが一体管理される
- ✅ ImageBufferの移動とともにパレットも移動
- ✅ SourceNodeでGIF/PNG読み込み時に自然にパレット設定可能

**デメリット**:
- ⚠️ ImageBufferのメモリフットプリント増加（最大1KB/画像）
- ⚠️ ダイレクトカラー画像には不要なメモリ

##### 案B: 専用のPaletteクラス

```cpp
// palette.h

class Palette {
public:
    Palette(size_t maxColors = 256);

    void setColor(size_t index, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

    const uint8_t* data() const;
    size_t size() const;

private:
    std::vector<uint8_t> colors_;  // RGBA8_Straight形式
};
```

ImageBufferは参照のみ保持：

```cpp
class ImageBuffer {
public:
    void setPaletteRef(const Palette* palette);
    const uint16_t* getPalette() const;

private:
    const Palette* paletteRef_ = nullptr;  // 非所有
};
```

**メリット**:
- ✅ 複数のImageBufferで同一パレット共有可能
- ✅ ImageBufferのメモリフットプリント増加なし
- ✅ パレット操作の専用APIが充実

**デメリット**:
- ⚠️ ライフタイム管理が複雑（dangling pointer のリスク）
- ⚠️ 新しいクラスの追加

##### 推奨：案Aの段階的実装

1. **Phase 1**: ImageBufferに `std::vector<uint16_t> palette_` 追加（最小実装）
2. **Phase 2**: 使用頻度が高ければPaletteクラスを追加（リファクタ）

---

### パレットフォーマットの柔軟性（重要な拡張提案）

#### 動機

現状の提案ではパレットを **RGBA8形式に固定** していたが、実用上は以下の問題がある：

1. **既存データとの非互換性**
   - GIFパレット: RGB888形式（3byte/色）
   - レトロハードウェア: RGB565形式（2byte/色）
   - PNGパレット: RGB888またはRGBA8形式
   - ユーザーが既にRGB565配列でパレットデータを保持している場合、無駄な変換が発生

2. **メモリオーバーヘッド**
   - RGB565パレット（256色）: 512 bytes
   - RGBA8パレット（256色）: 1,024 bytes（2倍）
   - 組み込み環境では無視できない差

3. **変換コスト**
   - RGB565 → RGBA8 → RGB565 のような往復変換は非効率

#### 提案：パレットもPixelFormatIDで管理

パレット自体もピクセルフォーマットとして扱い、柔軟に形式を選択可能にする。

##### ImageBuffer API拡張

```cpp
// image_buffer.h

class ImageBuffer {
public:
    // ... 既存フィールド ...

    // パレット情報（フォーマット指定版）
    void setPalette(const void* paletteData, size_t colorCount,
                    PixelFormatID paletteFormat);

    const void* getPaletteData() const;
    size_t getPaletteSize() const;
    PixelFormatID getPaletteFormat() const;
    bool hasPalette() const;

    // 後方互換用（RGBA8固定版）
    void setPaletteRGBA8(const uint8_t* palette, size_t colorCount) {
        setPalette(palette, colorCount, PixelFormatIDs::RGBA8_Straight);
    }

private:
    PixelFormatID format_;
    // ... 既存フィールド ...

    // パレット情報
    std::vector<uint8_t> paletteData_;  // 可変サイズ（フォーマット依存）
    PixelFormatID paletteFormat_ = nullptr;
    size_t paletteColorCount_ = 0;
};
```

##### 実装例

```cpp
void ImageBuffer::setPalette(const void* paletteData, size_t colorCount,
                             PixelFormatID paletteFormat) {
    if (!format_->isIndexed) {
        return;  // 警告: インデックスカラーではない
    }

    size_t maxColors = format_->maxPaletteSize;
    if (colorCount > maxColors) {
        colorCount = maxColors;
    }

    // パレットフォーマットに応じたバイト数を計算
    int bytesPerColor = getBytesPerPixel(paletteFormat);
    size_t totalBytes = colorCount * bytesPerColor;

    paletteData_.resize(totalBytes);
    std::memcpy(paletteData_.data(), paletteData, totalBytes);
    paletteFormat_ = paletteFormat;
    paletteColorCount_ = colorCount;
}

const void* ImageBuffer::getPaletteData() const {
    return paletteData_.empty() ? nullptr : paletteData_.data();
}

PixelFormatID ImageBuffer::getPaletteFormat() const {
    return paletteFormat_;
}
```

##### 変換関数の拡張

既存の変換インフラを活用してパレットを自動変換：

```cpp
// Index8 → RGBA8_Straight（パレットフォーマット対応版）
static void index8_toStandardIndexed_v2(const void* src, uint8_t* dst,
                                         int pixelCount,
                                         const void* paletteData,
                                         PixelFormatID paletteFormat) {
    const uint8_t* s = static_cast<const uint8_t*>(src);

    if (!paletteData || !paletteFormat) {
        // パレットなし: フォールバック
        return;
    }

    // パレットを標準フォーマット（RGBA8_Straight）に一時変換
    thread_local std::vector<uint8_t> standardPalette;
    int maxIndex = 256;  // Index8の場合
    standardPalette.resize(maxIndex * 4);  // RGBA8

    convertFormat(paletteData, paletteFormat,
                  standardPalette.data(), PixelFormatIDs::RGBA8_Straight,
                  maxIndex);

    // インデックスでパレット参照
    for (int i = 0; i < pixelCount; i++) {
        uint8_t index = s[i];
        const uint8_t* color = &standardPalette[index * 4];
        dst[i*4 + 0] = color[0];  // R
        dst[i*4 + 1] = color[1];  // G
        dst[i*4 + 2] = color[2];  // B
        dst[i*4 + 3] = color[3];  // A
    }
}
```

または、パフォーマンス重視の場合は直接変換：

```cpp
// RGB565パレット専用の最適化版
static void index8_toStandardIndexed_RGB565(const void* src, uint8_t* dst,
                                             int pixelCount,
                                             const uint16_t* paletteRGB565) {
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        uint8_t index = s[i];
        uint16_t color565 = paletteRGB565[index];

        // RGB565 → RGBA8 直接変換（最適化）
        uint8_t r5 = (color565 >> 11) & 0x1F;
        uint8_t g6 = (color565 >> 5) & 0x3F;
        uint8_t b5 = color565 & 0x1F;

        dst[i*4 + 0] = (r5 << 3) | (r5 >> 2);
        dst[i*4 + 1] = (g6 << 2) | (g6 >> 4);
        dst[i*4 + 2] = (b5 << 3) | (b5 >> 2);
        dst[i*4 + 3] = 255;
    }
}
```

##### convertFormat() の拡張

既存の `convertFormat()` にパレットフォーマット引数を追加：

```cpp
inline void convertFormat(const void* src, PixelFormatID srcFormat,
                          void* dst, PixelFormatID dstFormat,
                          int pixelCount,
                          const void* srcPaletteData = nullptr,
                          PixelFormatID srcPaletteFormat = nullptr,  // 新規追加
                          const void* dstPaletteData = nullptr,
                          PixelFormatID dstPaletteFormat = nullptr); // 新規追加
```

実装例：

```cpp
inline void convertFormat(...) {
    // ... 既存のチェック ...

    // インデックスカラー変換の場合
    if (srcFormat->isIndexed && srcFormat->toStandardIndexed) {
        // パレットを標準形式（RGBA8）に変換
        thread_local std::vector<uint8_t> standardPalette;
        size_t maxColors = srcFormat->maxPaletteSize;

        if (srcPaletteFormat && srcPaletteData) {
            standardPalette.resize(maxColors * 4);
            convertFormat(srcPaletteData, srcPaletteFormat,
                          standardPalette.data(), PixelFormatIDs::RGBA8_Straight,
                          maxColors);

            // RGBA8パレットとしてインデックス変換
            srcFormat->toStandardIndexed(src, conversionBuffer.data(),
                                        pixelCount,
                                        standardPalette.data());
        }
    }

    // ... 残りの処理 ...
}
```

#### 対応パレットフォーマット

| フォーマット | サイズ/色 | 用途 |
|-------------|----------|------|
| RGBA8_Straight | 4 bytes | **基本形式（推奨）** 透過GIF、PNG、パイプライン標準 |
| RGB565_LE/BE | 2 bytes | GIF、レトロゲーム、組み込み、メモリ効率重視 |
| RGB888 | 3 bytes | PNG、標準的なパレット |
| BGR888 | 3 bytes | 一部のハードウェア |
| RGB332 | 1 byte | 超低メモリ環境 |

#### 使用例

##### RGB565パレットでの運用

```cpp
// GIFデコーダ（RGB565パレット）
ImageBuffer decodeGIF_RGB565(const uint8_t* gifData, size_t dataSize) {
    // GIF解析
    int width = ...;
    int height = ...;
    const uint8_t* indexData = ...;
    const uint8_t* gifPalette = ...;  // RGB888
    int paletteSize = ...;

    // Index8バッファ作成
    ImageBuffer buffer(width, height, PixelFormatIDs::Index8);
    std::memcpy(buffer.pixels(), indexData, width * height);

    // パレット変換（RGB888 → RGB565）
    std::vector<uint16_t> paletteRGB565(paletteSize);
    for (int i = 0; i < paletteSize; i++) {
        uint8_t r = gifPalette[i*3 + 0];
        uint8_t g = gifPalette[i*3 + 1];
        uint8_t b = gifPalette[i*3 + 2];
        paletteRGB565[i] = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }

    // RGB565形式でパレット設定
    buffer.setPalette(paletteRGB565.data(), paletteSize,
                      PixelFormatIDs::RGB565_LE);

    return buffer;
}
```

##### ユーザー既存データの活用

```cpp
// ユーザーが既にRGB565配列で保持しているパレット
extern const uint16_t myCustomPaletteRGB565[256];

ImageBuffer applyCustomPalette(const ImageBuffer& indexBuffer) {
    ImageBuffer result = indexBuffer;  // コピー

    // ユーザーのRGB565パレットをそのまま使用（変換なし）
    result.setPalette(myCustomPaletteRGB565, 256,
                      PixelFormatIDs::RGB565_LE);

    return result;
}
```

##### メモリ効率の比較

```
256色パレット:
- RGB332:  256 bytes
- RGB565:  512 bytes
- RGB888:  768 bytes
- RGBA8:  1,024 bytes（基本形式）

RGB565採用で RGBA8比で 50% メモリ削減
```

#### メリット

- ✅ **ゼロコピー**: ユーザー既存のパレットデータをそのまま使用可能
- ✅ **メモリ効率**: 必要最小限のパレットフォーマットを選択
- ✅ **変換コスト削減**: 不要な往復変換を回避
- ✅ **既存インフラ活用**: `PixelFormatDescriptor` と `convertFormat()` を再利用
- ✅ **拡張性**: 将来的な新フォーマット追加が容易

#### デメリットと対策

| デメリット | 対策 |
|-----------|------|
| API複雑化 | 後方互換用の簡易版APIも提供 |
| 変換オーバーヘッド | よく使う組み合わせは最適化版を実装 |
| パレット形式の検証 | `isValidPaletteFormat()` でチェック |

#### 実装戦略

##### Phase 1: RGBA8固定版（基本実装）

```cpp
void setPalette(const uint8_t* palette, size_t colorCount);
```

パイプライン標準のRGBA8_Straightで最小実装。動作確認。

##### Phase 2: パレットフォーマット対応（本提案）

```cpp
void setPalette(const void* paletteData, size_t colorCount,
                PixelFormatID paletteFormat);
```

汎用的なパレット管理に拡張。RGB565等の他フォーマットにも対応。

##### Phase 3: 最適化パス（オプション）

よく使われる組み合わせ（Index8 + RGB565パレット）の直接変換を実装。

---

## 色量子化アルゴリズム

RGBA → Index8 変換には色量子化が必要。複数のアルゴリズムが存在：

### 1. 最近傍探索（Nearest Neighbor）

**概要**: 既存パレットから最も近い色を選ぶ

```cpp
// 上記の index8_fromStandardIndexed() で実装済み
```

**特徴**:
- ✅ 実装が単純
- ✅ 高速（O(N×P), N=ピクセル数, P=パレットサイズ）
- ⚠️ パレットが事前に決まっている必要がある

**用途**: GIF/PNGデコード（パレット既知）

### 2. Median Cut（中央値分割）

**概要**: RGB色空間を再帰的に分割してパレット生成

```cpp
// 疑似コード
std::vector<uint16_t> generatePaletteMedianCut(const uint8_t* pixels,
                                               int pixelCount,
                                               int targetColors) {
    // 1. 全ピクセルのRGB値を収集
    // 2. 色空間を最も分散の大きい軸で分割
    // 3. 再帰的に targetColors 個のボックスを作成
    // 4. 各ボックスの平均色をパレットエントリに
    // ...
}
```

**特徴**:
- ✅ 高品質なパレット生成
- ✅ 実装が比較的容易
- ⚠️ 計算コストが高い（O(N log N)）

**用途**: RGBA → Index8 変換時の自動パレット生成

### 3. Octree（八分木）

**概要**: RGB色空間を八分木で分割してパレット生成

**特徴**:
- ✅ Median Cutより高速
- ✅ インクリメンタルな処理が可能
- ⚠️ 実装が複雑

### 4. K-means クラスタリング

**概要**: k-means法で色空間をクラスタリング

**特徴**:
- ✅ 非常に高品質
- ❌ 計算コストが非常に高い
- ❌ 反復計算が必要

### 推奨実装戦略

#### Phase 1: 最近傍探索のみ

GIF/PNGデコード用途に限定し、パレットは外部から与える。

```cpp
// 既に実装済み（index8_fromStandardIndexed）
```

#### Phase 2: Median Cut追加

自動パレット生成機能を追加：

```cpp
// palette_generation.h/cpp (新規ファイル)

namespace PaletteGeneration {
    // Median Cut アルゴリズム（パレットはRGBA8_Straight形式で返す）
    std::vector<uint8_t> generateMedianCut(const uint8_t* rgba8Pixels,
                                           int pixelCount,
                                           int targetColors);

    // ユーティリティ
    std::vector<uint8_t> generateFromImage(const ImageBuffer& buffer,
                                           int targetColors);
}
```

#### Phase 3: ディザリング対応（オプション）

色量子化時の品質向上：

```cpp
namespace Dithering {
    // Floyd-Steinberg ディザリング（パレットはRGBA8_Straight形式）
    void floydSteinberg(const uint8_t* src, uint8_t* dst,
                        int width, int height,
                        const uint8_t* palette, int paletteSize);

    // Ordered ディザリング（Bayer matrix）
    void orderedDither(const uint8_t* src, uint8_t* dst,
                       int width, int height,
                       const uint8_t* palette, int paletteSize);
}
```

---

## 低ビット深度フォーマット（Index4/2/1）

### Index4（16色）

```cpp
const PixelFormatDescriptor Index4 = {
    "Index4",
    4,   // bitsPerPixel
    2,   // pixelsPerUnit (1バイトに2ピクセル)
    1,   // bytesPerUnit
    { ChannelDescriptor(4, 0), ChannelDescriptor(0, 0),
      ChannelDescriptor(0, 0), ChannelDescriptor(0, 0) },
    false, false,
    true,   // isIndexed
    16,     // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    nullptr, nullptr,
    index4_toStandardIndexed,
    index4_fromStandardIndexed
};
```

**変換実装**:

```cpp
static void index4_toStandardIndexed(const void* src, uint8_t* dst,
                                      int pixelCount, const uint8_t* palette) {
    const uint8_t* s = static_cast<const uint8_t*>(src);

    for (int i = 0; i < pixelCount; i++) {
        // 1バイトに2ピクセル（上位4bit, 下位4bit）
        uint8_t byte = s[i / 2];
        uint8_t index = (i % 2 == 0) ? (byte >> 4) : (byte & 0x0F);

        const uint8_t* color = &palette[index * 4];
        dst[i*4 + 0] = color[0];
        dst[i*4 + 1] = color[1];
        dst[i*4 + 2] = color[2];
        dst[i*4 + 3] = color[3];
    }
}
```

### Index2（4色）

CGA、Game Boy等のレトロハードウェア対応。

```cpp
const PixelFormatDescriptor Index2 = {
    "Index2",
    2,   // bitsPerPixel
    4,   // pixelsPerUnit (1バイトに4ピクセル)
    1,   // bytesPerUnit
    { ChannelDescriptor(2, 0), ChannelDescriptor(0, 0),
      ChannelDescriptor(0, 0), ChannelDescriptor(0, 0) },
    false, false,
    true,   // isIndexed
    4,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    nullptr, nullptr,
    index2_toStandardIndexed,
    index2_fromStandardIndexed
};
```

### Index1（2色、モノクロ）

1bit bitmap（白黒）。

```cpp
const PixelFormatDescriptor Index1 = {
    "Index1",
    1,   // bitsPerPixel
    8,   // pixelsPerUnit (1バイトに8ピクセル)
    1,   // bytesPerUnit
    { ChannelDescriptor(1, 0), ChannelDescriptor(0, 0),
      ChannelDescriptor(0, 0), ChannelDescriptor(0, 0) },
    false, false,
    true,   // isIndexed
    2,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    nullptr, nullptr,
    index1_toStandardIndexed,
    index1_fromStandardIndexed
};
```

**用途**: e-ink ディスプレイ、レトロゲーム（初代Game Boy等）

---

## Gray8とIndex8の相互変換

### Gray8 → Index8

グレースケールパレットを生成：

```cpp
// グレースケールパレット（256階調）を生成（RGBA8_Straight形式）
std::vector<uint8_t> createGrayscalePalette() {
    std::vector<uint8_t> palette(256 * 4);
    for (int i = 0; i < 256; i++) {
        palette[i*4 + 0] = i;    // R
        palette[i*4 + 1] = i;    // G
        palette[i*4 + 2] = i;    // B
        palette[i*4 + 3] = 255;  // A
    }
    return palette;
}

// Gray8 → Index8 変換（ピクセル値はそのまま、パレットを設定）
ImageBuffer gray8ToIndex8(const ImageBuffer& gray8Buffer) {
    ImageBuffer index8Buffer(gray8Buffer.width(), gray8Buffer.height(),
                             PixelFormatIDs::Index8);

    // ピクセルデータはメモリコピーのみ（値は同じ）
    std::memcpy(index8Buffer.pixels(), gray8Buffer.pixels(),
                gray8Buffer.width() * gray8Buffer.height());

    // グレースケールパレットを設定
    auto palette = createGrayscalePalette();
    index8Buffer.setPalette(palette.data(), 256);

    return index8Buffer;
}
```

### Index8 → Gray8

パレットの輝度値を計算：

```cpp
// Index8 → Gray8 変換（パレットから輝度計算）
ImageBuffer index8ToGray8(const ImageBuffer& index8Buffer) {
    const uint8_t* palette = index8Buffer.getPalette();
    if (!palette) {
        // パレットなし: そのままコピー
        // ...
    }

    ImageBuffer gray8Buffer(index8Buffer.width(), index8Buffer.height(),
                            PixelFormatIDs::Gray8);

    const uint8_t* src = index8Buffer.pixels();
    uint8_t* dst = gray8Buffer.pixels();
    int pixelCount = index8Buffer.width() * index8Buffer.height();

    for (int i = 0; i < pixelCount; i++) {
        uint8_t index = src[i];
        const uint8_t* color = &palette[index * 4];

        // RGBA8 → Gray8（輝度変換）
        uint16_t r = color[0];
        uint16_t g = color[1];
        uint16_t b = color[2];
        dst[i] = static_cast<uint8_t>((77*r + 150*g + 29*b) >> 8);
    }

    return gray8Buffer;
}
```

---

## ユースケース

### 1. GIF/PNGパレット画像のデコード

```cpp
// GIFデコーダでの利用例
ImageBuffer decodeGIF(const uint8_t* gifData, size_t dataSize) {
    // GIF解析（省略）
    int width = ...;
    int height = ...;
    const uint8_t* indexData = ...;      // インデックスデータ
    const uint8_t* gifPalette = ...;     // GIFパレット（RGB888）
    int paletteSize = ...;

    // Index8バッファ作成
    ImageBuffer buffer(width, height, PixelFormatIDs::Index8);
    std::memcpy(buffer.pixels(), indexData, width * height);

    // パレット変換（RGB888 → RGBA8_Straight）
    std::vector<uint8_t> palette(paletteSize * 4);
    for (int i = 0; i < paletteSize; i++) {
        palette[i*4 + 0] = gifPalette[i*3 + 0];  // R
        palette[i*4 + 1] = gifPalette[i*3 + 1];  // G
        palette[i*4 + 2] = gifPalette[i*3 + 2];  // B
        palette[i*4 + 3] = 255;                   // A
    }
    buffer.setPalette(palette.data(), paletteSize);

    return buffer;
}
```

### 2. 減色処理ノード（QuantizeNode）

```cpp
class QuantizeNode : public FilterNodeBase {
public:
    QuantizeNode(int targetColors = 256);

    void setColorCount(int count);
    void setDithering(bool enable);

protected:
    RenderResponse pullProcess(const RenderRequest& request) override {
        // 上流からRGBA画像取得
        RenderResponse input = upstreamNode(0)->pullProcess(request);

        // パレット生成（Median Cut）
        auto palette = PaletteGeneration::generateMedianCut(
            input.buffer.pixels(),
            input.buffer.width() * input.buffer.height(),
            targetColors_
        );

        // Index8バッファ作成
        ImageBuffer index8Buffer(input.buffer.width(), input.buffer.height(),
                                 PixelFormatIDs::Index8);
        index8Buffer.setPalette(palette.data(), targetColors_);

        // 色量子化（オプションでディザリング）
        if (enableDithering_) {
            Dithering::floydSteinberg(input.buffer.pixels(),
                                      index8Buffer.pixels(),
                                      input.buffer.width(), input.buffer.height(),
                                      palette.data(), targetColors_);
        } else {
            convertFormat(input.buffer.pixels(), input.buffer.format(),
                          index8Buffer.pixels(), index8Buffer.format(),
                          input.buffer.width() * input.buffer.height(),
                          nullptr, palette.data());
        }

        // 下流がRGBAを期待する場合は再変換
        // ...
    }

private:
    int targetColors_;
    bool enableDithering_;
};
```

### 3. レトロゲーム風エフェクト

```cpp
// Game Boy風（4色）パレット（RGBA8_Straight形式）
std::vector<uint8_t> createGameBoyPalette() {
    std::vector<uint8_t> palette(4 * 4);
    // 色0: 暗緑 (0x0F, 0x38, 0x0F)
    palette[0*4 + 0] = 0x0F; palette[0*4 + 1] = 0x38;
    palette[0*4 + 2] = 0x0F; palette[0*4 + 3] = 255;
    // 色1: 中緑 (0x30, 0x62, 0x30)
    palette[1*4 + 0] = 0x30; palette[1*4 + 1] = 0x62;
    palette[1*4 + 2] = 0x30; palette[1*4 + 3] = 255;
    // 色2: 薄緑 (0x8B, 0xAC, 0x0F)
    palette[2*4 + 0] = 0x8B; palette[2*4 + 1] = 0xAC;
    palette[2*4 + 2] = 0x0F; palette[2*4 + 3] = 255;
    // 色3: 黄緑 (0x9B, 0xBC, 0x0F)
    palette[3*4 + 0] = 0x9B; palette[3*4 + 1] = 0xBC;
    palette[3*4 + 2] = 0x0F; palette[3*4 + 3] = 255;
    return palette;
}

// Index2でGame Boy風に変換
ImageBuffer toGameBoyStyle(const ImageBuffer& rgba8Buffer) {
    ImageBuffer index2Buffer(rgba8Buffer.width(), rgba8Buffer.height(),
                             PixelFormatIDs::Index2);
    auto palette = createGameBoyPalette();
    index2Buffer.setPalette(palette.data(), 4);

    // ディザリング付き変換
    Dithering::floydSteinberg(rgba8Buffer.pixels(), index2Buffer.pixels(),
                              rgba8Buffer.width(), rgba8Buffer.height(),
                              palette.data(), 4);

    return index2Buffer;
}
```

---

## 考慮事項

### 1. パレット形式の標準化

パレットは **RGBA8_Straight形式** を内部標準とする理由：

- パイプライン標準のRGBA8_Straightと統一
- メモリ効率が良い
- アルファチャンネル対応（透過GIF等）

### 2. アルファ対応パレット

透過GIF等のためにアルファ対応：

```cpp
const PixelFormatDescriptor Index8_WithAlpha = {
    "Index8_WithAlpha",
    // ...
    true,   // hasAlpha ← パレットがアルファを含む
    // ...
};
```

または、フォーマットは同じでパレット内容で判断：

```cpp
bool Palette::hasTransparency() const {
    for (size_t i = 0; i < size(); i++) {
        if (colors_[i*4 + 3] < 255) {  // A < 1.0
            return true;
        }
    }
    return false;
}
```

### 3. パレット最適化

使用されていないパレットエントリの削減：

```cpp
// 実際に使用されているインデックスのみを抽出
std::vector<uint8_t> optimizePalette(const uint8_t* indexData, int pixelCount,
                                     const uint8_t* originalPalette) {
    std::set<uint8_t> usedIndices;
    for (int i = 0; i < pixelCount; i++) {
        usedIndices.insert(indexData[i]);
    }

    // 使用されているエントリのみを新パレットに
    std::vector<uint8_t> optimizedPalette(usedIndices.size() * 4);
    // ...
}
```

### 4. エンディアン

低ビット深度（Index4/2/1）でのビット順序：

```cpp
enum class BitOrder {
    MSBFirst,  // 最上位ビットが先（Index4: 上位4bitが先）
    LSBFirst   // 最下位ビットが先
};
```

GIF/PNG仕様に合わせてMSBFirstが一般的。

---

## 実装順序

### Phase 1: Index8基本実装

1. `PixelFormatDescriptor` で Index8 定義
2. `index8_toStandardIndexed()` / `index8_fromStandardIndexed()` 実装
   - パレット参照（toStandard）
   - 最近傍探索（fromStandard）
3. `ImageBuffer` にパレット管理機能追加
   - `setPalette()` / `getPalette()`
4. テストケース作成

**工数**: 1-2日

### Phase 2: パレット生成機能

1. `palette_generation.h/cpp` 新規作成
2. Median Cut アルゴリズム実装
3. `PaletteGeneration::generateFromImage()` API
4. テスト追加

**工数**: 2-3日

### Phase 3: ディザリング対応

1. `dithering.h/cpp` 新規作成
2. Floyd-Steinberg ディザリング実装
3. Ordered ディザリング実装（オプション）
4. テスト追加

**工数**: 1-2日

### Phase 4: 低ビット深度（オプション）

1. Index4/2/1 フォーマット追加
2. ビットパッキング処理実装
3. レトロハードウェアパレットプリセット

**工数**: 2-3日

### Phase 5: QuantizeNode実装（オプション）

1. ノードクラス作成
2. パラメータ設定API
3. WebUIバインディング

**工数**: 1-2日

---

## テスト戦略

### 単体テスト

```cpp
TEST_CASE("Index8 palette lookup") {
    // パレット作成（4色）
    uint16_t palette[4 * 4] = {
        0xFF00, 0x0000, 0x0000, 0xFFFF,  // Red
        0x0000, 0xFF00, 0x0000, 0xFFFF,  // Green
        0x0000, 0x0000, 0xFF00, 0xFFFF,  // Blue
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF   // White
    };

    // Index8データ
    uint8_t indexData[4] = {0, 1, 2, 3};

    // RGBA8に変換
    uint8_t rgba8[4 * 4];
    index8_toStandardIndexed(indexData, rgba8, 4, palette);

    // 検証
    REQUIRE(rgba8[0*4 + 0] == 255);  // Red R
    REQUIRE(rgba8[1*4 + 1] == 255);  // Green G
    REQUIRE(rgba8[2*4 + 2] == 255);  // Blue B
    REQUIRE(rgba8[3*4 + 0] == 255);  // White R
}

TEST_CASE("Nearest color quantization") {
    // 簡易パレット（白黒）
    uint16_t palette[2 * 4] = {
        0x0000, 0x0000, 0x0000, 0xFFFF,  // Black
        0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF   // White
    };

    // グレー画像（中間色）
    uint8_t rgba8[4] = {128, 128, 128, 255};

    // Index8に変換（最近傍）
    uint8_t indexData[1];
    index8_fromStandardIndexed(rgba8, indexData, 1, palette);

    // 128は255に近い → index 1（White）
    REQUIRE(indexData[0] == 1);
}
```

### 統合テスト

```cpp
TEST_CASE("ImageBuffer with palette") {
    ImageBuffer index8Buffer(16, 16, PixelFormatIDs::Index8);

    // パレット設定
    auto palette = createGrayscalePalette();
    index8Buffer.setPalette(palette.data(), 256);

    REQUIRE(index8Buffer.hasPalette() == true);
    REQUIRE(index8Buffer.getPaletteSize() == 256);

    // RGBA8に変換
    ImageBuffer rgba8Buffer(16, 16, PixelFormatIDs::RGBA8_Straight);
    convertFormat(index8Buffer.pixels(), index8Buffer.format(),
                  rgba8Buffer.pixels(), rgba8Buffer.format(),
                  16 * 16,
                  index8Buffer.getPalette(), nullptr);

    // 検証 ...
}
```

---

## 関連

- **IDEA_SINGLE_CHANNEL_PIXEL_FORMATS.md**: Gray8フォーマット（非パレット版）
- **IDEA_ALPHA_MERGE_SPLIT_NODES.md**: シングルチャンネルのユースケース
- **pixel_format.h/cpp**: 実装対象ファイル
- **image_buffer.h/cpp**: パレット管理機能の追加先

---

## 結論

**推奨**: Phase 1でIndex8とパレット管理機能を実装し、GIF/PNGデコード用途に対応。パレット生成やディザリングは需要に応じてPhase 2以降で追加する段階的アプローチが最適。

Gray8とIndex8は**別フォーマットとして定義**し、メモリレイアウトは同じだが意味と変換方法を区別する。
