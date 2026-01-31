# Alpha8ピクセルフォーマット

> **ステータス**: ✅ 実装完了
>
> このアイデアは実装されました。詳細は [DESIGN_CHANNEL_DESCRIPTOR.md](../DESIGN_CHANNEL_DESCRIPTOR.md) を参照してください。
> 以下は当初の構想ドキュメントです（参考用）。

## 概要

アルファマスクを効率的に扱うため、シングルチャンネル（1チャンネル）のピクセルフォーマット **Alpha8** を追加する。

- **Alpha8**: 8bit アルファチャンネル（透明度、1byte/pixel）

## 動機

### 必要性

**AlphaMerge/SplitNodeの前提条件**
- AlphaSplitNode: RGBA → RGB + **Alpha画像**
- AlphaMergeNode: RGB + **Alpha画像** → RGBA
- アルファチャンネルを単独の画像として扱う必要がある

### メモリ効率

現状はグレースケールやアルファマスクを扱うには RGBA8 (4byte/pixel) を使用する必要があるが、Alpha8 (1byte/pixel) により **75%のメモリ削減**が可能。

```
512×512 アルファマスク:
- 現状 (RGBA8): 1,048,576 bytes
- 提案 (Alpha8):   262,144 bytes (75%削減)
```

### 実用シーン

- AlphaMerge/SplitNodeでのアルファチャンネル分離・合成
- マスク画像の処理（アルファマスク、深度マップ等）
- 合成処理のワークフロー

---

## Alpha8の設計

### フォーマット定義

```cpp
const PixelFormatDescriptor Alpha8 = {
    "Alpha8",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    { ChannelDescriptor(8, 0),   // Alpha value
      ChannelDescriptor(0, 0),   // No G
      ChannelDescriptor(0, 0),   // No B
      ChannelDescriptor(0, 0) }, // No A (data is alpha itself)
    true,   // hasAlpha = true
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    alpha8_toStandard,
    alpha8_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr   // fromStandardIndexed
};
```

### 標準変換（RGBA8_Straight経由）

#### Alpha8 → RGBA8_Straight

アルファ値をグレースケールとして可視化（デバッグ用）：

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

**設計意図**: 可視化した時にアルファマスクの内容を確認できる

#### RGBA8_Straight → Alpha8

Aチャンネルを抽出：

```cpp
static void alpha8_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        d[i] = src[i*4 + 3];  // Aチャンネルのみ抽出
    }
}
```

---

## 実装仕様

### pixel_format.cpp への追加

```cpp
// ========================================================================
// Alpha8: 8bit Alpha Channel
// ========================================================================

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

static void alpha8_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        d[i] = src[i*4 + 3];  // Aチャンネルのみ抽出
    }
}

namespace BuiltinFormats {

const PixelFormatDescriptor Alpha8 = {
    "Alpha8",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    { ChannelDescriptor(8, 0),   // Alpha value
      ChannelDescriptor(0, 0),
      ChannelDescriptor(0, 0),
      ChannelDescriptor(0, 0) },
    true,   // hasAlpha
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    alpha8_toStandard,
    alpha8_fromStandard,
    nullptr,
    nullptr
};

} // namespace BuiltinFormats
```

### pixel_format.h への追加

```cpp
namespace BuiltinFormats {
    extern const PixelFormatDescriptor RGBA8_Straight;
    extern const PixelFormatDescriptor RGB565_LE;
    extern const PixelFormatDescriptor RGB565_BE;
    extern const PixelFormatDescriptor RGB332;
    extern const PixelFormatDescriptor RGB888;
    extern const PixelFormatDescriptor BGR888;
    extern const PixelFormatDescriptor Alpha8;  // 追加
}

namespace PixelFormatIDs {
    // 8bit RGBA系
    inline const PixelFormatID RGBA8_Straight = &BuiltinFormats::RGBA8_Straight;

    // パックドRGB系
    inline const PixelFormatID RGB565_LE = &BuiltinFormats::RGB565_LE;
    inline const PixelFormatID RGB565_BE = &BuiltinFormats::RGB565_BE;
    inline const PixelFormatID RGB332 = &BuiltinFormats::RGB332;

    // 24bit RGB系
    inline const PixelFormatID RGB888 = &BuiltinFormats::RGB888;
    inline const PixelFormatID BGR888 = &BuiltinFormats::BGR888;

    // シングルチャンネル系
    inline const PixelFormatID Alpha8 = &BuiltinFormats::Alpha8;  // 追加
}

// 組み込みフォーマット一覧
inline const PixelFormatID builtinFormats[] = {
    PixelFormatIDs::RGBA8_Straight,
    PixelFormatIDs::RGB565_LE,
    PixelFormatIDs::RGB565_BE,
    PixelFormatIDs::RGB332,
    PixelFormatIDs::RGB888,
    PixelFormatIDs::BGR888,
    PixelFormatIDs::Alpha8,  // 追加
};
```

---

## テスト戦略

### 単体テスト

```cpp
// Alpha8変換テスト
TEST_CASE("Alpha8 conversion") {
    // 1. RGBA → Alpha8（Aチャンネル抽出）
    uint8_t rgba[4] = {100, 150, 200, 128};  // A=128
    uint8_t alpha[1];
    alpha8_fromStandard(rgba, alpha, 1);
    REQUIRE(alpha[0] == 128);  // Aチャンネルのみ抽出

    // 2. Alpha8 → RGBA（アルファ展開）
    uint8_t alpha_src[1] = {200};
    uint8_t rgba_dst[4];
    alpha8_toStandard(alpha_src, rgba_dst, 1);
    REQUIRE(rgba_dst[0] == 200);  // R = alpha
    REQUIRE(rgba_dst[1] == 200);  // G = alpha
    REQUIRE(rgba_dst[2] == 200);  // B = alpha
    REQUIRE(rgba_dst[3] == 200);  // A = alpha

    // 3. ラウンドトリップ
    uint8_t original_alpha[1] = {64};
    uint8_t temp_rgba[4];
    uint8_t recovered_alpha[1];
    alpha8_toStandard(original_alpha, temp_rgba, 1);
    alpha8_fromStandard(temp_rgba, recovered_alpha, 1);
    REQUIRE(recovered_alpha[0] == 64);  // ロスレス
}
```

### 統合テスト

```cpp
// AlphaSplitNode → AlphaMergeNode ラウンドトリップ
TEST_CASE("Alpha split-merge round trip") {
    // 1. 入力RGBA画像作成
    ImageBuffer rgba_input(512, 512, PixelFormatIDs::RGBA8_Straight);
    // ... 画像データ設定 ...

    // 2. AlphaSplitNode
    AlphaSplitNode splitNode;
    splitNode.connect(sourceNode);
    auto [rgb_output, alpha_output] = splitNode.process(...);

    // alpha_output のフォーマット検証
    REQUIRE(alpha_output.format() == PixelFormatIDs::Alpha8);

    // 3. AlphaMergeNode
    AlphaMergeNode mergeNode;
    mergeNode.connect(0, rgbNode);
    mergeNode.connect(1, alphaNode);
    ImageBuffer rgba_recovered = mergeNode.process(...);

    // 4. 入力と復元結果の比較
    REQUIRE(rgba_input == rgba_recovered);  // ロスレス
}
```

---

## 将来の拡張

### Gray8フォーマット

グレースケール画像処理が必要になった時点で追加：

```cpp
const PixelFormatDescriptor Gray8 = {
    "Gray8",
    8, 1, 1,
    { ChannelDescriptor(8, 0), ... },
    false,  // hasAlpha = false (Alpha8との違い)
    // ...
    gray8_toStandard,    // 輝度展開 (R=G=B=gray)
    gray8_fromStandard,  // 輝度計算 (ITU-R BT.601)
};
```

**Gray8 vs Alpha8の違い**:
- メモリレイアウトは同じ（1byte/pixel）
- `hasAlpha` フラグが異なる
- 標準変換の挙動が異なる（輝度計算 vs Aチャンネル抽出）
- 意味が異なる（輝度値 vs 透明度）

### ChannelExtractNode

任意のチャンネルを任意のフォーマットに抽出する柔軟性を提供：

```cpp
class ChannelExtractNode : public FilterNodeBase {
public:
    enum class SourceChannel { Red = 0, Green = 1, Blue = 2, Alpha = 3 };
    enum class OutputFormat { Gray, Alpha };

    void setSourceChannel(SourceChannel channel);
    void setOutputFormat(OutputFormat format);
};
```

**ユースケース例**:
- グリーンバック画像のGチャンネルをAlpha8として抽出
- RチャンネルをGray8として抽出

**実装タイミング**: Gray8フォーマット追加時に合わせて実装

### パフォーマンス最適化

現在の実装（RGBA8_Straight経由）でボトルネックが確認された場合：

#### 専用変換経路の追加

```cpp
struct PixelFormatDescriptor {
    // 既存フィールド...

    // シングルチャンネル専用の変換関数（オプション）
    using ToAlphaFunc = void(*)(const void* src, uint8_t* dst, int pixelCount);
    using FromAlphaFunc = void(*)(const uint8_t* src, void* dst, int pixelCount);

    ToAlphaFunc toAlpha;      // このフォーマット → Alpha8 直接変換
    FromAlphaFunc fromAlpha;  // Alpha8 → このフォーマット 直接変換
};
```

**改善効果**:
- 中間バッファが1byte/pixelに削減（75%削減）
- 合計メモリコピー量が50%削減

**実装タイミング**: プロファイリングでボトルネックが確認された時点

---

## 実装手順

1. **pixel_format.cpp に変換関数実装**
   - `alpha8_toStandard()` - アルファ可視化
   - `alpha8_fromStandard()` - Aチャンネル抽出

2. **BuiltinFormats::Alpha8 定義**
   - `hasAlpha = true`
   - bitsPerPixel = 8
   - bytesPerUnit = 1

3. **pixel_format.h にエクスポート**
   - `extern const PixelFormatDescriptor Alpha8`
   - `PixelFormatIDs::Alpha8`
   - `builtinFormats` 配列に追加

4. **テストケース作成**
   - Alpha8 ↔ RGBA8 変換の正確性
   - ラウンドトリップテスト
   - 境界値テスト（0, 128, 255）

5. **AlphaMerge/SplitNodeでの利用**
   - AlphaSplitNode: RGBA → RGB + Alpha8
   - AlphaMergeNode: RGB + Alpha8 → RGBA

---

## 関連ドキュメント

- **IDEA_ALPHA_MERGE_SPLIT_NODES.md**: Alpha8フォーマットの主要ユースケース
- **pixel_format.h/cpp**: 実装対象ファイル
