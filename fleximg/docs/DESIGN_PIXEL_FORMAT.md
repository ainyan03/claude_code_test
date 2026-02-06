# ピクセルフォーマット設計

## 現在のデフォルトフォーマット

**RGBA8_Straight** を全ての内部処理で使用しています。

| フォーマット | 用途 | ステータス |
|-------------|------|-----------|
| RGBA8_Straight | 入出力、合成、フィルタ処理 | **デフォルト** |

- **フォーマット変換オーバーヘッドの削減**: 中間フォーマットへの変換が不要
- **メモリ使用量の削減**: 4バイト/ピクセル
- **キャッシュ効率の向上**: 特に組み込み環境で効果的

---

## Under合成（blendUnderStraight）

### 概要

CompositeNodeは **under合成** を使用します。背景（dst）側がStraight形式のキャンバスバッファで、
前景（src）を「下に敷く」形で合成します。

```
結果 = src + dst × (1 - srcAlpha)
```

### 関数シグネチャ

```cpp
// 統一シグネチャ（全変換関数共通）
using ConvertFunc = void(*)(void* dst, const void* src, int pixelCount, const PixelAuxInfo* aux);

// PixelFormatDescriptorのメンバ
ConvertFunc toStraight;        // このフォーマット → RGBA8_Straight
ConvertFunc fromStraight;      // RGBA8_Straight → このフォーマット
ConvertFunc expandIndex;       // インデックス展開（インデックスフォーマット用、非インデックスはnullptr）
ConvertFunc blendUnderStraight;// under合成（src → Straight dst）
ConvertFunc swapEndian;        // エンディアン兄弟との変換

const PixelFormatDescriptor* siblingEndian;  // エンディアン違いの兄弟フォーマット
```

### PixelAuxInfo構造体（旧ConvertParams）

```cpp
struct PixelAuxInfo {
    // パレット情報（インデックスフォーマット用）
    const void* palette = nullptr;           // パレットデータポインタ（非所有）
    PixelFormatID paletteFormat = nullptr;   // パレットエントリのフォーマット
    uint16_t paletteColorCount = 0;          // パレットエントリ数

    uint8_t alphaMultiplier = 255;  // アルファ係数（1 byte）AlphaNodeで使用
    uint32_t colorKeyRGBA8 = 0;    // カラーキー比較値（RGBA8、alpha込み）
    uint32_t colorKeyReplace = 0;  // カラーキー差し替え値（通常は透明黒0）
    // colorKeyRGBA8 == colorKeyReplace の場合は無効
};

// 後方互換エイリアス
using ConvertParams = PixelAuxInfo;
using BlendParams = PixelAuxInfo;
```

### 対応フォーマット

| フォーマット | 関数 | 特徴 |
|-------------|------|------|
| RGBA8_Straight | rgba8_blendUnderStraight | ストレートアルファで合成 |
| RGB565_LE | rgb565le_blendUnderStraight | 不透明として処理（α=255） |
| RGB565_BE | rgb565be_blendUnderStraight | 不透明として処理（α=255） |
| RGB332 | rgb332_blendUnderStraight | 不透明として処理（α=255） |
| RGB888 | rgb888_blendUnderStraight | 不透明として処理（α=255） |
| BGR888 | bgr888_blendUnderStraight | 不透明として処理（α=255） |
| Alpha8 | alpha8_blendUnderStraight | グレースケール×α |
| Grayscale8 | — | BT.601輝度、toStraight/fromStraight対応 |
| Index8 | — | expandIndex経由でパレット展開 |

### 使用例（CompositeNode）

```cpp
// キャンバスはStraight形式で初期化
ImageBuffer canvas = createCanvas(width, height);  // RGBA8_Straight

// 各入力を順にunder合成
for (auto& input : inputs) {
    const auto* desc = input.buffer.formatID();
    if (desc && desc->blendUnderStraight) {
        desc->blendUnderStraight(
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
- **拡張性**: 新フォーマット追加時は `blendUnderStraight` 関数を実装するだけ

### 8bit精度ブレンド

全てのblendUnderStraight関数は**8bit精度**でブレンド計算を行います。
これにより、全フォーマット間で一貫した動作が保証されます。

```cpp
// 8bit精度ブレンド（全blendUnderStraight共通）
uint_fast8_t dstA = d[3];
uint_fast16_t invDstA = 255 - dstA;
d[0] = d[0] + ((src_r * invDstA + 127) / 255);
```

**設計意図**:
- 全フォーマットで同一のブレンド結果を保証
- SWAR最適化との整合性（8bit単位での処理）

### SWAR最適化

blendUnderStraight関数にはSWAR（SIMD Within A Register）最適化が適用されています。
32ビットレジスタにRG/BAの2チャンネルを同時にパックして演算することで、乗算回数を削減しています。

---

## 関連ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/image/pixel_format.h` | 型定義、ユーティリティ、各フォーマットinclude |
| `src/fleximg/image/pixel_format/` | 各フォーマット実装（stb-style） |
| ├── `rgba8_straight.h` | RGBA8_Straight |
| ├── `alpha8.h` | Alpha8 |
| ├── `rgb565.h` | RGB565_LE/BE + ルックアップテーブル |
| ├── `rgb332.h` | RGB332 + ルックアップテーブル |
| ├── `rgb888.h` | RGB888/BGR888 |
| ├── `grayscale8.h` | Grayscale8（BT.601輝度） |
| └── `index8.h` | Index8（パレットインデックス、expandIndex） |
| `src/fleximg/operations/canvas_utils.h` | キャンバス作成・合成（RGBA8_Straight固定） |

## PixelFormatID

PixelFormatID は `const PixelFormatDescriptor*` として定義されており、Descriptorへのポインタがそのままフォーマット識別子として機能します。

```cpp
using PixelFormatID = const PixelFormatDescriptor*;

// 組み込みフォーマット
namespace PixelFormatIDs {
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
    myToStraightFunc,           // toStraight
    myFromStraightFunc,         // fromStraight
    nullptr,                    // expandIndex（インデックスフォーマットの場合のみ）
    myBlendUnderStraightFunc,   // blendUnderStraight（オプション）
    nullptr, nullptr            // siblingEndian, swapEndian
};

// 使用
PixelFormatID myFormat = &MyCustomFormat;
```

---

## インデックスフォーマットの変換フロー

### expandIndex

インデックスフォーマット（Index8）は `toStraight` / `fromStraight` の代わりに `expandIndex` 関数を持ちます。
`expandIndex` はインデックス値をパレットフォーマットのピクセルデータに展開します。

```
Index8 → expandIndex → パレットフォーマットのピクセルデータ
```

### convertFormat でのインデックス変換

`convertFormat` はインデックスフォーマットを自動判定し、以下のフローで変換します:

```
【1段階変換】パレットフォーマット == 出力フォーマットの場合:
  Index → expandIndex → dst（直接展開）

【2段階変換】パレットフォーマット != 出力フォーマットの場合:
  Index → expandIndex → パレットフォーマット → [toStraight →] [fromStraight →] dst
```

### ImageBuffer のパレット管理

```cpp
// パレット付きIndex8画像の作成
ImageBuffer buf(width, height, PixelFormatIDs::Index8);
buf.setPalette(paletteData, PixelFormatIDs::RGBA8_Straight, 256);

// toFormat() でパレット情報が自動的に convertFormat に渡される
ImageBuffer rgba = std::move(buf).toFormat(PixelFormatIDs::RGBA8_Straight);
```

パレットデータは非所有ポインタとして保持されます。コピー/ムーブ時にポインタが伝播されますが、パレットデータの寿命管理は呼び出し側の責任です。

---

## 関連ドキュメント

- [BENCHMARK_BLEND_UNDER.md](BENCHMARK_BLEND_UNDER.md) - blendUnder関数のベンチマーク結果
- [ARCHITECTURE.md](ARCHITECTURE.md) - 全体アーキテクチャ
