# ChannelDescriptor設計とAlpha8フォーマット

## 概要

ChannelDescriptor構造体を拡張し、各チャンネルに**明示的な種別（ChannelType）**を持たせる設計を導入しました。これにより、シングルチャンネルフォーマット（Alpha8等）の実装が可能になりました。

## ChannelType列挙型

各チャンネルが何を表すかを明示的に識別する列挙型です。

```cpp
enum class ChannelType : uint8_t {
    Unused = 0,      // チャンネルなし
    Red,             // 赤チャンネル
    Green,           // 緑チャンネル
    Blue,            // 青チャンネル
    Alpha,           // アルファチャンネル
    Luminance,       // 輝度（グレースケール）
    Index            // パレットインデックス
};
```

## ChannelDescriptor構造体

### フィールド定義

```cpp
struct ChannelDescriptor {
    ChannelType type;   // チャンネル種別（Phase 1で追加）
    uint8_t bits;       // ビット数（0なら存在しない）
    uint8_t shift;      // ビットシフト量
    uint16_t mask;      // ビットマスク

    // コンストラクタ（チャンネル種別を指定）
    constexpr ChannelDescriptor(ChannelType t, uint8_t b, uint8_t s = 0);
};
```

### 設計上の重要な決定

**配列インデックス = メモリレイアウト順序**

`PixelFormatDescriptor::channels[4]` 配列の順序は、**メモリ上のバイト順序**を表します。

```cpp
// RGBA8_Straight: バイトオーダーが R, G, B, A の順
channels[0] = Red チャンネル（メモリ上の1バイト目）
channels[1] = Green チャンネル（メモリ上の2バイト目）
channels[2] = Blue チャンネル（メモリ上の3バイト目）
channels[3] = Alpha チャンネル（メモリ上の4バイト目）

// Alpha8: バイトオーダーが A の順
channels[0] = Alpha チャンネル（メモリ上の1バイト目）
channels[1] = Unused（存在しない）
channels[2] = Unused（存在しない）
channels[3] = Unused（存在しない）
```

**意味的な順序ではなく物理的な順序**を表現することで、ピクセルフォーマットのメモリレイアウトが自明になります。

## メソッドベースアクセスAPI

### PixelFormatDescriptorに追加されたメソッド

Phase 2で以下の4つのメソッドを追加しました：

#### 1. getChannel(uint8_t index)

```cpp
// 指定インデックスのチャンネル記述子を取得
constexpr ChannelDescriptor getChannel(uint8_t index) const {
    return (index < channelCount) ? channels[index] : ChannelDescriptor();
}
```

**用途**: メモリ順序で各チャンネルの詳細を取得

```cpp
// 使用例: RGBA8_Straightの最初のチャンネル（メモリ上の1バイト目）を取得
auto ch = RGBA8_Straight->getChannel(0);
// ch.type == ChannelType::Red
// ch.bits == 8
```

#### 2. getChannelIndex(ChannelType type)

```cpp
// 指定タイプのチャンネルインデックスを取得（見つからない場合は-1）
constexpr int8_t getChannelIndex(ChannelType type) const {
    for (uint8_t i = 0; i < channelCount; ++i) {
        if (channels[i].type == type) {
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}
```

**用途**: 特定の種別のチャンネルがメモリ上の何バイト目にあるか検索

```cpp
// 使用例: RGBA8_Straightでアルファチャンネルの位置を検索
int8_t alphaIndex = RGBA8_Straight->getChannelIndex(ChannelType::Alpha);
// alphaIndex == 3（メモリ上の4バイト目）
```

#### 3. hasChannelType(ChannelType type)

```cpp
// 指定タイプのチャンネルを持つか判定
constexpr bool hasChannelType(ChannelType type) const {
    return getChannelIndex(type) >= 0;
}
```

**用途**: フォーマットが特定の種別のチャンネルを持つか高速判定

```cpp
// 使用例: フォーマットにアルファチャンネルがあるか確認
if (format->hasChannelType(ChannelType::Alpha)) {
    // アルファチャンネルあり
}
```

#### 4. getChannelByType(ChannelType type)

```cpp
// 指定タイプのチャンネル記述子を取得（見つからない場合はUnusedを返す）
constexpr ChannelDescriptor getChannelByType(ChannelType type) const {
    int8_t idx = getChannelIndex(type);
    return (idx >= 0) ? channels[idx] : ChannelDescriptor();
}
```

**用途**: 種別を指定してチャンネルの詳細を取得

```cpp
// 使用例: アルファチャンネルの詳細を取得
auto alphaChannel = format->getChannelByType(ChannelType::Alpha);
if (alphaChannel.bits > 0) {
    // アルファチャンネルあり、ビット数はalphaChannel.bits
}
```

## Alpha8フォーマット

### 概要

単一チャンネル（アルファのみ）の8bitピクセルフォーマットです。

- **メモリ効率**: 1byte/pixel（RGBA8比75%削減）
- **用途**: アルファマスク、AlphaSplitNode/MergeNodeの出力/入力

### フォーマット定義

```cpp
const PixelFormatDescriptor Alpha8 = {
    "Alpha8",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    1,   // channelCount
    { ChannelDescriptor(ChannelType::Alpha, 8, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    true,   // hasAlpha
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    alpha8_toStandard,
    alpha8_fromStandard,
    nullptr, nullptr
};
```

### 標準変換（RGBA8_Straight経由）

#### Alpha8 → RGBA8_Straight（可視化）

アルファ値を全チャンネルに展開し、グレースケールとして可視化します。

```cpp
static void alpha8_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t alpha = s[i];
        dst[i*4 + 0] = alpha;  // R
        dst[i*4 + 1] = alpha;  // G
        dst[i*4 + 2] = alpha;  // B
        dst[i*4 + 3] = alpha;  // A
    }
}
```

**設計意図**: デバッグ時にアルファマスクの内容を視覚的に確認可能

#### RGBA8_Straight → Alpha8（抽出）

Aチャンネルのみを抽出します。

```cpp
static void alpha8_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        d[i] = src[i*4 + 3];  // Aチャンネルのみ抽出
    }
}
```

### メモリレイアウト

```
Alpha8 (1byte/pixel):
+-------+
| Alpha |
+-------+
 byte 0

RGBA8_Straight (4bytes/pixel):
+---+---+---+---+
| R | G | B | A |
+---+---+---+---+
  0   1   2   3
```

## 既存フォーマットの更新

全8フォーマットにChannelTypeを明示的に設定しました（Phase 3-4）。

### 例: RGBA8_Straight

```cpp
const PixelFormatDescriptor RGBA8_Straight = {
    "RGBA8_Straight",
    32, 1, 4, 4,  // bitsPerPixel, pixelsPerUnit, bytesPerUnit, channelCount
    { ChannelDescriptor(ChannelType::Red, 8, 0),
      ChannelDescriptor(ChannelType::Green, 8, 0),
      ChannelDescriptor(ChannelType::Blue, 8, 0),
      ChannelDescriptor(ChannelType::Alpha, 8, 0) },
    true, false, false, 0,
    // ...
};
```

### 例: RGB565_LE（パックドRGB）

```cpp
const PixelFormatDescriptor RGB565_LE = {
    "RGB565_LE",
    16, 1, 2, 3,  // channelCount = 3
    { ChannelDescriptor(ChannelType::Red, 5, 11),    // bits=5, shift=11
      ChannelDescriptor(ChannelType::Green, 6, 5),   // bits=6, shift=5
      ChannelDescriptor(ChannelType::Blue, 5, 0),    // bits=5, shift=0
      ChannelDescriptor() },  // Unused
    false, false, false, 0,
    // ...
};
```

## ユースケース

### 1. アルファチャンネルの有無を確認

```cpp
if (format->hasChannelType(ChannelType::Alpha)) {
    // アルファブレンディングが必要
}
```

### 2. チャンネルのビット数を取得

```cpp
auto redChannel = format->getChannelByType(ChannelType::Red);
if (redChannel.bits == 5) {
    // 5bit赤チャンネル（RGB565等）
}
```

### 3. メモリレイアウトの走査

```cpp
for (uint8_t i = 0; i < format->channelCount; ++i) {
    auto ch = format->getChannel(i);
    printf("Channel %d: type=%d, bits=%d\n", i, ch.type, ch.bits);
}
```

## テスト

42の包括的なテストケースを追加しました（Phase 6）：

- ChannelTypeとアクセサメソッドの動作確認（全8フォーマット）
- Alpha8変換の正確性テスト
- ラウンドトリップテスト
- 境界値テスト（0, 128, 255）

```cpp
TEST_CASE("PixelFormatDescriptor channel methods") {
    SUBCASE("Alpha8 - single channel") {
        const auto* fmt = PixelFormatIDs::Alpha8;
        CHECK(fmt->channelCount == 1);
        CHECK(fmt->getChannel(0).type == ChannelType::Alpha);
        CHECK(fmt->hasChannelType(ChannelType::Alpha) == true);
        CHECK(fmt->hasChannelType(ChannelType::Red) == false);
    }
}
```

## 将来の拡張

### Gray8フォーマット（輝度チャンネル）

グレースケール画像処理が必要になった時点で追加予定：

```cpp
const PixelFormatDescriptor Gray8 = {
    "Gray8",
    8, 1, 1, 1,
    { ChannelDescriptor(ChannelType::Luminance, 8, 0),
      ChannelDescriptor(), ChannelDescriptor(), ChannelDescriptor() },
    false,  // hasAlpha = false（Alpha8との違い）
    // ...
};
```

**Gray8 vs Alpha8の違い**:
- メモリレイアウトは同じ（1byte/pixel）
- `ChannelType` が異なる（Luminance vs Alpha）
- `hasAlpha` フラグが異なる（false vs true）
- 意味が異なる（輝度値 vs 透明度）

### パフォーマンス最適化

現在の実装は標準フォーマット（RGBA8_Straight）経由の変換ですが、プロファイリングでボトルネックが確認された場合、専用の直接変換関数を追加可能です。

## 関連ドキュメント

| ドキュメント | 内容 |
|-------------|------|
| DESIGN_PIXEL_FORMAT.md | RGBA8変換アルゴリズム |
| IDEA_ALPHA_MERGE_SPLIT_NODES.md | Alpha8の主要ユースケース |
| pixel_format.h/cpp | 実装ファイル |

## 実装ファイル

| ファイル | 役割 |
|---------|------|
| `src/fleximg/image/pixel_format.h` | ChannelType enum、ChannelDescriptor、アクセサメソッド、ユーティリティ関数 |
| `src/fleximg/image/pixel_format/` | 各フォーマット実装（stb-style） |
| ├── `rgba8_straight.h` | RGBA8_Straight + invUnpremulTable |
| ├── `alpha8.h` | Alpha8 |
| ├── `rgb565.h` | RGB565_LE/BE + ルックアップテーブル |
| ├── `rgb332.h` | RGB332 + ルックアップテーブル |
| └── `rgb888.h` | RGB888/BGR888 |
| `test/pixel_format_test.cpp` | 94テストケース |
