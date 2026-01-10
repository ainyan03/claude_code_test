# ピクセルフォーマット変換設計

RGBA8_Straight と RGBA16_Premultiplied 間の変換処理について説明します。

## フォーマット概要

| フォーマット | 用途 |
|-------------|------|
| RGBA8_Straight | 入出力、画像保存、フィルタ処理 |
| RGBA16_Premultiplied | 合成・アフィン変換処理 |

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
namespace PixelFormatIDs {
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
}
```

### 使用例（合成処理）

```cpp
using namespace PixelFormatIDs::RGBA16Premul;

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

| ファイル | 役割 |
|---------|------|
| `src/fleximg/pixel_format.h` | フォーマットID、閾値定数 |
| `src/fleximg/pixel_format_registry.cpp` | 変換関数実装 |
| `src/fleximg/operations/blend.cpp` | 合成処理での閾値判定 |
