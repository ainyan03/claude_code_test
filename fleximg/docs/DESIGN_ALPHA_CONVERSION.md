# アルファ変換方式の設計

## 概要

RGBA8_Straight と RGBA16_Premultiplied 間の変換処理を最適化し、除算処理を削減する。

## 背景

### 現状の問題

#### 問題1: 除算コスト

従来の変換方式では、ピクセルごとに複数回の除算が発生：

```cpp
// RGBA8_Straight → RGBA16_Premultiplied (fromStandard)
r16 = (r8 << 8) | r8;           // 8bit → 16bit 拡張
a16 = (a8 << 8) | a8;
d[idx] = (r16 * a16 + 32767) / 65535;  // 除算3回

// RGBA16_Premultiplied → RGBA8_Straight (toStandard)
r_unpre = (r16 * 65535) / a16;   // 除算3回（変動除数）
```

- Forward変換: `/65535` が3回（固定除数だが重い）
- Reverse変換: `/a16` が3回（変動除数、さらに重い）

#### 問題2: 透明ピクセルでのRGB情報喪失

Premultiplied形式では、アルファ値が0の場合にRGB値も0になる：

```
入力: A8=0, R8=255, G8=128, B8=64（透明だがRGB情報を持つ）
従来方式での変換後: A16=0, R16=0, G16=0, B16=0（RGB情報喪失）
```

これにより、将来「アルファ値を濃くするフィルタ」を実装した場合に、
元のRGB情報を復元できない問題が発生する。

**問題点まとめ**:
- 除算処理が重い
- 透明ピクセルでRGB情報が失われる

## 新方式の設計

### 基本方針

1. 16bit Premultiplied の有効桁数を上位8bitに限定
2. 下位8bit成分は「おまけ」として扱う
3. `A_tmp = A8 + 1` を使い、ゼロ除算を回避しつつ計算を簡略化

### 変換アルゴリズム

#### RGBA8_Straight → RGBA16_Premultiplied

```cpp
A_tmp = A8 + 1;          // 範囲: 1-256
A16 = 255 * A_tmp;       // 範囲: 255-65280
R16 = R8 * A_tmp;        // 範囲: 0-65280
G16 = G8 * A_tmp;
B16 = B8 * A_tmp;
```

**特徴**: 除算ゼロ、乗算のみ

#### RGBA16_Premultiplied → RGBA8_Straight

```cpp
A8 = A16 >> 8;           // 範囲: 0-255
A_tmp = A8 + 1;          // 範囲: 1-256
R8 = R16 / A_tmp;        // 除数が1-256に限定
G8 = G16 / A_tmp;
B8 = B16 / A_tmp;
```

**特徴**: 除数が1-256の範囲に限定（テーブル化やSIMD最適化が容易）

### アルファ値の範囲

| A8 (Straight) | A16 (Premultiplied) | 意味 |
|---------------|---------------------|------|
| 0 | 255 | 透明 |
| 1 | 510 | ほぼ透明 |
| 128 | 32895 | 半透明 |
| 255 | 65280 | 不透明 |

**注意**: A16の範囲は255-65280となり、0と65535は使用されない。

### RGB情報の保持

この方式では、A8=0（完全透明）でもRGB情報が保持される：

```
入力: A8=0, R8=255, G8=128, B8=64
変換後: A16=255, R16=255, G16=128, B16=64
```

これにより「アルファ値を濃くするフィルタ」が将来実装された場合にも、
RGB情報を復元可能。

## アルファ閾値の定義

### 透明・不透明の判定

A16の範囲が非標準（255-65280）のため、合成処理での判定基準を明確化：

| 判定 | 条件 | 説明 |
|------|------|------|
| 透明 | `A16 <= 255` | 合成をスキップ |
| 半透明 | `256 <= A16 < 65280` | 通常の合成処理 |
| 不透明 | `A16 >= 65280` | 最適化パス（コピーのみ） |

### 定数の定義場所

`pixel_format.h` の `PixelFormatIDs` 名前空間内に、フォーマット固有の定数を定義：

```cpp
namespace PixelFormatIDs {
    constexpr PixelFormatID RGBA16_Premultiplied = 0x0002;

    // RGBA16_Premultiplied 用のアルファ閾値（constexpr）
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

    // 透明スキップ（constexpr比較、オーバーヘッドなし）
    if (srcA <= ALPHA_TRANSPARENT_MAX) continue;

    // 不透明最適化
    if (srcA >= ALPHA_OPAQUE_MIN && dstA == 0) {
        // コピーのみ
    } else {
        // 通常の合成
    }
}
```

## PixelFormatDescriptor への拡張（オプション）

汎用性のため、`PixelFormatDescriptor` にも閾値フィールドを追加：

```cpp
struct PixelFormatDescriptor {
    // ... 既存フィールド ...

    // アルファ閾値（Premultiplied形式用）
    uint16_t alphaTransparentMax = 0;      // デフォルト: 0
    uint16_t alphaOpaqueMin = 0xFFFF;      // デフォルト: 最大値
};
```

これにより、ループ前に閾値を取得してローカル変数に保存する使い方も可能：

```cpp
const auto* fmt = registry.getFormat(formatID);
const uint16_t transMax = fmt->alphaTransparentMax;

for (...) {
    if (srcA <= transMax) continue;  // 単純比較
}
```

## 性能評価

| 処理 | 従来方式 | 新方式 | 改善 |
|------|---------|--------|------|
| Forward (8→16) 除算 | 3回 `/65535` | 0回 | 100%削減 |
| Reverse (16→8) 除算 | 3回 `/a16` (変動) | 3回 `/A_tmp` (1-256) | 除数範囲縮小 |
| 透明判定 | `== 0` | `<= 255` | 同等 |
| 不透明判定 | `== 65535` | `>= 65280` | 同等 |

## 実装対象ファイル

1. **pixel_format.h**: `RGBA16Premul` 名前空間と定数を追加
2. **pixel_format_registry.cpp**: 変換関数を新方式に更新
3. **operators.cpp**: 合成処理の閾値判定を更新
4. **test/viewport_test.cpp**: 新しい閾値に対応したテストを追加

## 互換性

### 内部互換性
- 既存のパイプライン処理は影響を受けない
- 合成処理の閾値判定を更新するのみ

### 外部互換性
- WASM API経由の入出力は RGBA8_Straight のため影響なし
- 内部の16bit表現が変わるが、外部からは不可視

## 将来の拡張

### Reverse変換の更なる最適化

除数が1-256に限定されるため、以下の最適化が可能：

1. **ルックアップテーブル**: 256エントリの逆数テーブル（512バイト）
2. **SIMD最適化**: 固定パターンの除算をベクトル化

### 他のPremultiplied形式への適用

同様のアプローチは `RGBA8_Premultiplied` にも適用可能：

```cpp
namespace RGBA8Premul {
    constexpr uint8_t ALPHA_TRANSPARENT_MAX = 0;
    constexpr uint8_t ALPHA_OPAQUE_MIN = 255;
}
```
