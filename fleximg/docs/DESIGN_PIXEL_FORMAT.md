# ピクセルフォーマット設計

## 現在のデフォルトフォーマット

**RGBA8_Straight** を全ての内部処理で使用しています。

| フォーマット | 用途 | ステータス |
|-------------|------|-----------|
| RGBA8_Straight | 入出力、合成、フィルタ処理 | **デフォルト** |
| RGBA16_Premultiplied | 高精度合成 | `FLEXIMG_ENABLE_PREMUL` で有効化 |

### RGBA8_Straight への移行理由

ベンチマーク比較により、以下の結果が得られました：

| シナリオ | RGBA16版 FPS | RGBA8版 FPS | 改善率 |
|---------|-------------|-------------|--------|
| Source | 29.9 | 34.2 | +14% |
| Affine | ~30 | 34.3 | +14% |
| Composite | 11.6 | 16.2 | **+40%** |
| Matte | 12.3 | 12.5 | +2% |

- **フォーマット変換オーバーヘッドの削減**: RGBA8→RGBA16変換が不要に
- **メモリ使用量の削減**: 4バイト/ピクセル vs 8バイト/ピクセル（-50%）
- **キャッシュ効率の向上**: 特に組み込み環境で効果的

---

## Under合成（blendUnderPremul）

### 概要

CompositeNodeは **under合成** を使用します。背景（dst）側がPremultiplied形式のキャンバスバッファで、
前景（src）を「下に敷く」形で合成します。

```
結果 = src + dst × (1 - srcAlpha)
```

### 関数シグネチャ

```cpp
// 統一シグネチャ（全変換関数共通）
using ConvertFunc = void(*)(void* dst, const void* src, int pixelCount, const ConvertParams* params);

// PixelFormatDescriptorのメンバ
ConvertFunc toStraight;        // このフォーマット → RGBA8_Straight
ConvertFunc fromStraight;      // RGBA8_Straight → このフォーマット
ConvertFunc toPremul;          // このフォーマット → RGBA16_Premultiplied
ConvertFunc fromPremul;        // RGBA16_Premultiplied → このフォーマット
ConvertFunc blendUnderPremul;  // under合成（src → Premul dst）
ConvertFunc swapEndian;        // エンディアン兄弟との変換

const PixelFormatDescriptor* siblingEndian;  // エンディアン違いの兄弟フォーマット
```

### ConvertParams構造体

```cpp
struct ConvertParams {
    uint32_t colorKey = 0;          // 透過カラー（4 bytes）
    uint8_t alphaMultiplier = 255;  // アルファ係数（1 byte）
    bool useColorKey = false;       // カラーキー有効フラグ（1 byte）
};

// 後方互換性のためBlendParamsをエイリアスとして残す
using BlendParams = ConvertParams;
```

### 対応フォーマット

| フォーマット | 関数 | 特徴 |
|-------------|------|------|
| RGBA8_Straight | rgba8_blendUnderPremul | ストレートアルファを内部でPremulに変換 |
| RGB565_LE | rgb565le_blendUnderPremul | 不透明として処理（α=255） |
| RGB565_BE | rgb565be_blendUnderPremul | 不透明として処理（α=255） |
| RGB332 | rgb332_blendUnderPremul | 不透明として処理（α=255） |
| RGB888 | rgb888_blendUnderPremul | 不透明として処理（α=255） |
| BGR888 | bgr888_blendUnderPremul | 不透明として処理（α=255） |
| Alpha8 | alpha8_blendUnderPremul | グレースケール×α |

### 使用例（CompositeNode）

```cpp
// キャンバスはPremultiplied形式で初期化
ImageBuffer canvas = createCanvas(width, height);  // RGBA8_Premul相当

// 各入力を順にunder合成
for (auto& input : inputs) {
    const auto* desc = input.buffer.formatID();
    if (desc && desc->blendUnderPremul) {
        desc->blendUnderPremul(
            input.buffer.data(),
            canvas.data() + offset,
            pixelCount,
            params
        );
    }
}
```

### 設計意図

- **フォーマット変換の削減**: 入力画像を標準形式に変換せず、直接合成
- **メモリ効率**: 中間バッファ不要
- **拡張性**: 新フォーマット追加時は `blendUnderPremul` 関数を実装するだけ

### 8bit精度ブレンド

全てのblendUnderPremul関数は**8bit精度**でブレンド計算を行います。
これにより、全フォーマット間で一貫した動作が保証されます。

```cpp
// 8bit精度ブレンド（全blendUnderPremul共通）
uint_fast8_t dstA8 = d[idx + 3] >> 8;   // 16bit→8bit
uint_fast16_t invDstA8 = 255 - dstA8;
d[idx] = d[idx] + src8 * invDstA8;       // 結果は16bitに蓄積
```

**設計意図**:
- 全フォーマットで同一のブレンド結果を保証
- SWAR最適化との整合性（8bit単位での処理）
- 16bit蓄積により多層合成での丸め誤差累積を軽減

### SWAR最適化

blendUnderPremulおよびtoPremul関数にはSWAR（SIMD Within A Register）最適化が適用されています。
32ビットレジスタにRG/BAの2チャンネルを同時にパックして演算することで、乗算回数を削減しています。

```cpp
// 例: toPremul（RGを同時処理）
uint32_t rg = (r + (static_cast<uint32_t>(g) << 16)) * a_tmp;
// rg & 0xFFFF = R16, rg >> 16 = G16
```

> **TODO**: rgba16Premul_blendUnderPremulにもSWAR最適化を適用予定

### ラウンドトリップ精度

toPremul → fromPremul のラウンドトリップ変換は**精度100%**を達成しています。
これは`invUnpremulTable`の計算にceil（切り上げ）を使用することで実現しています。

```cpp
// invUnpremulTable[a] = ceil(65536 / (a+1))
constexpr uint16_t calcInvUnpremul(int a) {
    return (a == 0) ? 0 : static_cast<uint16_t>(
        (65536u + static_cast<uint32_t>(a)) / static_cast<uint32_t>(a + 1));
}
```

---

## 参考: RGBA16_Premultiplied 仕様（オプション）

以下はRGBA16_Premultiplied使用時の仕様です。`FLEXIMG_ENABLE_PREMUL` を定義することで有効化できます。
高精度な合成が必要な場合に使用してください（C++17必須、メモリ使用量2倍）。

### RGBA8_Straight ↔ RGBA16_Premultiplied 変換

## 変換アルゴリズム

### RGBA8_Straight → RGBA16_Premultiplied

```cpp
A_tmp = A8 + 1;          // 範囲: 1-256（ゼロ除算回避）
A16 = 255 * A_tmp;       // 範囲: 255-65280
R16 = R8 * A_tmp;        // 範囲: 0-65280
G16 = G8 * A_tmp;
B16 = B8 * A_tmp;
```

**特徴**: 除算ゼロ、乗算のみ

### RGBA16_Premultiplied → RGBA8_Straight

```cpp
A8 = A16 >> 8;           // 範囲: 0-255
A_tmp = A8 + 1;          // 範囲: 1-256
R8 = R16 / A_tmp;        // 除数が1-256に限定
G8 = G16 / A_tmp;
B8 = B16 / A_tmp;
```

**特徴**: 除数が1-256の範囲に限定（テーブル化やSIMD最適化が容易）

## アルファ値の範囲

| A8 (Straight) | A16 (Premultiplied) | 意味 |
|---------------|---------------------|------|
| 0 | 255 | 透明 |
| 1 | 510 | ほぼ透明 |
| 128 | 32895 | 半透明 |
| 255 | 65280 | 不透明 |

**注意**: A16の範囲は255-65280となり、0と65535は使用されない

## RGB情報の保持

この方式では、A8=0（完全透明）でもRGB情報が保持されます：

```
入力: A8=0, R8=255, G8=128, B8=64
変換後: A16=255, R16=255, G16=128, B16=64
```

これにより「アルファ値を濃くするフィルタ」が将来実装された場合にもRGB情報を復元可能。

## アルファ閾値

A16の範囲が非標準（255-65280）のため、合成処理での判定基準を定義しています。

### 定数定義（pixel_format.h）

```cpp
namespace RGBA16Premul {
    constexpr uint16_t ALPHA_TRANSPARENT_MAX = 255;   // この値以下は透明
    constexpr uint16_t ALPHA_OPAQUE_MIN = 65280;      // この値以上は不透明

    inline constexpr bool isTransparent(uint16_t a) {
        return a <= ALPHA_TRANSPARENT_MAX;
    }
    inline constexpr bool isOpaque(uint16_t a) {
        return a >= ALPHA_OPAQUE_MIN;
    }
}
```

### 使用例（合成処理）

```cpp
using namespace RGBA16Premul;

for (int i = 0; i < pixelCount; i++) {
    uint16_t srcA = srcPixel[3];

    // 透明スキップ
    if (srcA <= ALPHA_TRANSPARENT_MAX) continue;

    // 不透明最適化
    if (srcA >= ALPHA_OPAQUE_MIN && dstA == 0) {
        // コピーのみ（ブレンド計算不要）
    } else {
        // 通常の合成
    }
}
```

## 性能特性

| 処理 | 特徴 |
|------|------|
| Forward (8→16) | 除算0回、乗算のみ |
| Reverse (16→8) | 除数範囲1-256（最適化可能）|
| 透明判定 | constexpr比較、オーバーヘッドなし |

## 関連ファイル

| ファイル | 役割 | RGBA16関連 |
|---------|------|-----------|
| `src/fleximg/image/pixel_format.h` | フォーマットID、変換関数 | `#if 0` で無効化 |
| `src/fleximg/image/pixel_format.cpp` | Descriptor実体、変換関数実装 | `#if 0` で無効化 |
| `src/fleximg/operations/canvas_utils.h` | キャンバス作成・合成 | RGBA8_Straight固定 |

## PixelFormatID

PixelFormatID は `const PixelFormatDescriptor*` として定義されており、Descriptorへのポインタがそのままフォーマット識別子として機能します。

```cpp
using PixelFormatID = const PixelFormatDescriptor*;

// 組み込みフォーマット
namespace PixelFormatIDs {
    inline const PixelFormatID RGBA16_Premultiplied = &BuiltinFormats::RGBA16_Premultiplied;
    inline const PixelFormatID RGBA8_Straight = &BuiltinFormats::RGBA8_Straight;
    // ...
}
```

### ユーザー定義フォーマット

ユーザーは `constexpr PixelFormatDescriptor` を定義するだけで独自フォーマットを追加できます。

```cpp
constexpr PixelFormatDescriptor MyCustomFormat = {
    "MyCustomFormat",
    32,  // bitsPerPixel
    // ... 他のフィールド
    myToStraightFunc,      // toStraight
    myFromStraightFunc,    // fromStraight
    nullptr, nullptr,      // toStraightIndexed, fromStraightIndexed
    myToPremulFunc,        // toPremul（オプション）
    myFromPremulFunc,      // fromPremul（オプション）
    myBlendUnderPremulFunc,// blendUnderPremul（オプション）
    nullptr, nullptr       // siblingEndian, swapEndian
};

// 使用
PixelFormatID myFormat = &MyCustomFormat;
```

---

## 関連ドキュメント

- [BENCHMARK_BLEND_UNDER.md](BENCHMARK_BLEND_UNDER.md) - blendUnder関数のベンチマーク結果
- [ARCHITECTURE.md](ARCHITECTURE.md) - 全体アーキテクチャ
