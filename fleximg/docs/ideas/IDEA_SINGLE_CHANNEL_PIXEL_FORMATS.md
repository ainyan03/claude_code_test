# シングルチャンネルピクセルフォーマット（Single-Channel Pixel Formats）

## 概要

グレースケール画像やアルファマスクを効率的に扱うため、シングルチャンネル（1チャンネル）のピクセルフォーマットを追加する。

- **Gray8**: 8bit グレースケール（輝度値、1byte/pixel）
- **Gray16**: 16bit グレースケール（輝度値、2byte/pixel）
- **Alpha8**: 8bit アルファチャンネル（透明度、1byte/pixel）
- **Alpha16**: 16bit アルファチャンネル（透明度、2byte/pixel）

## 動機

### 必要性

1. **AlphaMerge/SplitNodeの前提条件**
   - AlphaSplitNode: RGBA → RGB + **Alpha画像**（グレースケール）
   - AlphaMergeNode: RGB + **Alpha画像**（グレースケール）→ RGBA
   - アルファチャンネルを単独の画像として扱う必要がある

2. **メモリ効率**
   - 現状: グレースケールを扱うには RGBA8 (4byte/pixel) を使用
   - 提案: Gray8 (1byte/pixel) で **75%のメモリ削減**
   ```
   512×512 グレースケール画像:
   - 現状 (RGBA8): 1,048,576 bytes
   - 提案 (Gray8):   262,144 bytes (75%削減)
   ```

3. **実用シーン**
   - マスク画像の処理（アルファマスク、深度マップ等）
   - グレースケール画像処理パイプライン
   - 深度センサーやモーションキャプチャデータの統合

### 現状の制約

既存の `PixelFormatDescriptor` は4チャンネル（RGBA）を前提とした設計：

```cpp
struct PixelFormatDescriptor {
    ChannelDescriptor channels[4];  // R, G, B, A の固定配列
    // ...
};
```

全ての変換が `RGBA8_Straight` (4byte/pixel) を中間フォーマットとして経由するため、シングルチャンネルデータは **4倍のメモリコピーが発生**。

---

## 命名と設計方針

### Gray8 vs Alpha8 vs Single8

シングルチャンネル（1byte/pixel）のフォーマットには複数の命名案が考えられるが、**意味ごとに別のPixelFormatDescriptorとして定義する**方針を採用する。

| フォーマット | メモリレイアウト | 意味 | 標準変換 |
|-------------|----------------|------|---------|
| **Gray8** | 1byte/pixel | 輝度値（グレースケール） | RGB展開: R=G=B=gray<br>輝度計算: gray=ITU-R BT.601 |
| **Alpha8** | 1byte/pixel | 透明度（アルファチャンネル） | RGBA展開: A=alpha<br>A抽出: alpha=A |
| Index8 | 1byte/pixel | パレットインデックス | パレット参照 |

### 設計の根拠

#### 1. 意味の明確性

```cpp
// AlphaSplitNode の出力
outputs_[0] = ImageBuffer(..., PixelFormatIDs::RGBA8_Straight);  // RGB
outputs_[1] = ImageBuffer(..., PixelFormatIDs::Alpha8);          // Alpha ← 意味的に正しい

// グレースケール画像
ImageBuffer grayscale(..., PixelFormatIDs::Gray8);  // 輝度値
```

- **Gray8**: 輝度値として扱う（業界標準、OpenCV/PIL等と一貫）
- **Alpha8**: 透明度として扱う（AlphaMerge/Splitと整合）
- **型安全性**: 用途を間違えにくい

#### 2. 変換関数の分離

メモリレイアウトは同じでも、**変換関数の挙動が異なる**：

```cpp
// Gray8 → RGBA8_Straight（輝度として展開）
gray8_toStandard():
  dst[i*4 + 0] = gray;  // R = gray
  dst[i*4 + 1] = gray;  // G = gray
  dst[i*4 + 2] = gray;  // B = gray
  dst[i*4 + 3] = 255;   // A = 不透明

// RGBA8_Straight → Gray8（輝度計算）
gray8_fromStandard():
  gray = (77*R + 150*G + 29*B) >> 8  // ITU-R BT.601

// Alpha8 → RGBA8_Straight（アルファとして展開）
alpha8_toStandard():
  dst[i*4 + 0] = alpha;  // R = alpha（可視化用）
  dst[i*4 + 1] = alpha;  // G = alpha
  dst[i*4 + 2] = alpha;  // B = alpha
  dst[i*4 + 3] = alpha;  // A = alpha

// RGBA8_Straight → Alpha8（Aチャンネル抽出）
alpha8_fromStandard():
  alpha = src[i*4 + 3]  // Aチャンネルのみ
```

#### 3. hasAlphaフラグによる区別

```cpp
const PixelFormatDescriptor Gray8 = {
    "Gray8",
    // ...
    false,  // hasAlpha = false
    // ...
};

const PixelFormatDescriptor Alpha8 = {
    "Alpha8",
    // ...
    true,   // hasAlpha = true ← 重要な違い
    // ...
};
```

### Gray8 ⇔ Alpha8 の相互変換

意味的には異なるが、メモリレイアウトが同じため**相互変換を許可**する。

#### 変換の許可（警告付き）

```cpp
// convertFormat() での実装
inline void convertFormat(const void* src, PixelFormatID srcFormat,
                          void* dst, PixelFormatID dstFormat,
                          int pixelCount, ...) {
    // 同一フォーマット
    if (srcFormat == dstFormat) {
        std::memcpy(dst, src, pixelCount);
        return;
    }

    // Gray8 ⇔ Alpha8 の直接変換（バイナリ互換）
    if ((srcFormat == PixelFormatIDs::Gray8 && dstFormat == PixelFormatIDs::Alpha8) ||
        (srcFormat == PixelFormatIDs::Alpha8 && dstFormat == PixelFormatIDs::Gray8)) {

        #ifdef DEBUG
        fprintf(stderr, "Warning: Converting between Gray8 and Alpha8 "
                       "(semantic mismatch but binary compatible)\n");
        #endif

        // ピクセル値はそのままコピー
        std::memcpy(dst, src, pixelCount);
        return;
    }

    // その他は標準変換経由
    // ...
}
```

**設計意図**:
- ✅ ユーザーが明示的に変換を指示できる
- ✅ 警告で意味的な違いを通知
- ✅ メモリレイアウトが同じなので安全

### チャンネル抽出の柔軟性

ユーザーが**任意のチャンネルを任意のフォーマットに抽出**できる機能を提供する。

#### ユースケース：グリーンバック→アルファ抽出

```
グリーンバック画像（RGBA8）
  ↓ Gチャンネル抽出
Alpha画像（Alpha8）← グリーンの強度をアルファ値として使用
```

#### 提案：ChannelExtractNode

```cpp
// channel_extract_node.h

class ChannelExtractNode : public FilterNodeBase {
public:
    enum class SourceChannel {
        Red = 0,
        Green = 1,
        Blue = 2,
        Alpha = 3
    };

    enum class OutputFormat {
        Gray,   // Gray8として出力
        Alpha   // Alpha8として出力
    };

    ChannelExtractNode();

    void setSourceChannel(SourceChannel channel);
    void setOutputFormat(OutputFormat format);

protected:
    RenderResult pullProcess(const RenderRequest& request) override;

private:
    SourceChannel sourceChannel_ = SourceChannel::Alpha;
    OutputFormat outputFormat_ = OutputFormat::Alpha;
};
```

**実装例**:

```cpp
RenderResult ChannelExtractNode::pullProcess(const RenderRequest& request) {
    RenderResult input = upstreamNode(0)->pullProcess(request);

    // 出力フォーマット決定
    PixelFormatID outputFormat = (outputFormat_ == OutputFormat::Gray)
        ? PixelFormatIDs::Gray8
        : PixelFormatIDs::Alpha8;

    ImageBuffer output(input.buffer.width(), input.buffer.height(), outputFormat);

    // チャンネル抽出
    const uint8_t* src = input.buffer.pixels();
    uint8_t* dst = output.pixels();
    int pixelCount = input.buffer.width() * input.buffer.height();
    int channelOffset = static_cast<int>(sourceChannel_);

    for (int i = 0; i < pixelCount; i++) {
        dst[i] = src[i*4 + channelOffset];
    }

    return RenderResult(output);
}
```

**使用例**:

```cpp
// グリーンバック画像のGチャンネルをアルファとして抽出
SourceNode* greenbackImage = ...;
ChannelExtractNode* extractGreen = new ChannelExtractNode();
extractGreen->setSourceChannel(ChannelExtractNode::SourceChannel::Green);
extractGreen->setOutputFormat(ChannelExtractNode::OutputFormat::Alpha);
extractGreen->connect(greenbackImage);

// 出力はAlpha8フォーマット（Gチャンネルの値）
ImageBuffer alphaFromGreen = extractGreen->process(...);

// AlphaMergeNodeで元画像と合成
AlphaMergeNode* merge = new AlphaMergeNode();
merge->connect(0, greenbackImage);     // RGB入力
merge->connect(1, extractGreen);       // Alpha入力（G由来）
```

#### 低レベルユーティリティ（オプション）

```cpp
// channel_utils.h/cpp

namespace ChannelUtils {

// 任意のチャンネルを抽出
void extractChannel(const uint8_t* rgba8Src,
                   void* dst,
                   PixelFormatID dstFormat,  // Gray8 or Alpha8
                   int pixelCount,
                   int sourceChannel);  // 0=R, 1=G, 2=B, 3=A

// グリーンバック用の便利関数
void extractGreenAsAlpha(const uint8_t* rgba8Src,
                        uint8_t* alpha8Dst,
                        int pixelCount) {
    extractChannel(rgba8Src, alpha8Dst, PixelFormatIDs::Alpha8,
                  pixelCount, 1);  // G channel
}

// アルファを任意のチャンネルに注入
void injectAlpha(const uint8_t* alpha8Src,
                uint8_t* rgba8Dst,
                int pixelCount,
                int targetChannel = 3);  // デフォルトはAチャンネル

} // namespace ChannelUtils
```

### レイヤー構造

異なるレベルで異なる柔軟性を提供：

```
レベル1: PixelFormatDescriptor（型定義）
  - Gray8とAlpha8は別の型
  - 意味と標準変換が異なる
  - hasAlphaフラグで区別

レベル2: convertFormat（標準変換）
  - 定義された標準変換を実行
  - Gray8 ⇔ Alpha8 直接変換を許可（警告付き）

レベル3: ノード・ユーティリティ（柔軟な操作）
  - ChannelExtractNode: 任意チャンネル抽出
  - ChannelUtils: 低レベル操作関数
  - ユーザーが完全な制御権を持つ
```

---

## 提案

### アプローチ比較

| 項目 | 案A: 既存活用 | 案B: 専用経路 | 案C: 型システム拡張 |
|------|--------------|--------------|-------------------|
| 実装コスト | 低 | 中 | 高 |
| メモリ効率 | 中（4byte中間） | 高（1byte中間） | 高 |
| 拡張性 | 低 | 中 | 高 |
| 後方互換性 | 高 | 高 | 低 |
| 推奨タイミング | Phase 1 | Phase 2 | 将来検討 |

---

## 案A: 既存の仕組みを活用（最小コスト実装）

### 設計方針

- グレースケールを「RGBAのRチャンネル位置」として扱う
- 既存の `RGBA8_Straight` 経由変換を利用
- `PixelFormatDescriptor` 構造は変更しない

### 実装

#### フォーマット定義

```cpp
// pixel_format.cpp

namespace BuiltinFormats {

const PixelFormatDescriptor Gray8 = {
    "Gray8",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    { ChannelDescriptor(8, 0),   // Gray値をRチャンネル位置に配置
      ChannelDescriptor(0, 0),   // G: なし
      ChannelDescriptor(0, 0),   // B: なし
      ChannelDescriptor(0, 0) }, // A: なし
    false,  // hasAlpha
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    gray8_toStandard,
    gray8_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr   // fromStandardIndexed
};

const PixelFormatDescriptor Gray16 = {
    "Gray16",
    16,  // bitsPerPixel
    1,   // pixelsPerUnit
    2,   // bytesPerUnit
    { ChannelDescriptor(16, 0),  // Gray値をRチャンネル位置に配置
      ChannelDescriptor(0, 0),
      ChannelDescriptor(0, 0),
      ChannelDescriptor(0, 0) },
    false, false, false, 0,
    BitOrder::MSBFirst,
    ByteOrder::Native,
    gray16_toStandard,
    gray16_fromStandard,
    nullptr, nullptr
};

} // namespace BuiltinFormats
```

#### 変換関数

```cpp
// Gray8 → RGBA8_Straight
static void gray8_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t gray = s[i];
        dst[i*4 + 0] = gray;  // R
        dst[i*4 + 1] = gray;  // G
        dst[i*4 + 2] = gray;  // B
        dst[i*4 + 3] = 255;   // A (不透明)
    }
}

// RGBA8_Straight → Gray8
static void gray8_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        // ITU-R BT.601 輝度変換（Y = 0.299R + 0.587G + 0.114B）
        // 整数演算版: Y = (77R + 150G + 29B) / 256
        uint16_t r = src[i*4 + 0];
        uint16_t g = src[i*4 + 1];
        uint16_t b = src[i*4 + 2];
        d[i] = static_cast<uint8_t>((77*r + 150*g + 29*b) >> 8);

        // または単純にRチャンネルを採用（AlphaMerge/Split用途）:
        // d[i] = src[i*4 + 0];
    }
}

// Gray16 → RGBA8_Straight
static void gray16_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    const uint16_t* s = static_cast<const uint16_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t gray8 = s[i] >> 8;  // 16bit → 8bit
        dst[i*4 + 0] = gray8;
        dst[i*4 + 1] = gray8;
        dst[i*4 + 2] = gray8;
        dst[i*4 + 3] = 255;
    }
}

// RGBA8_Straight → Gray16
static void gray16_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    uint16_t* d = static_cast<uint16_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint16_t r = src[i*4 + 0];
        uint16_t g = src[i*4 + 1];
        uint16_t b = src[i*4 + 2];
        // 輝度計算後、16bit化
        uint16_t gray8 = (77*r + 150*g + 29*b) >> 8;
        d[i] = gray8 << 8;  // 8bit → 16bit (上位バイトに配置)
    }
}
```

#### エクスポート

```cpp
// pixel_format.h

namespace BuiltinFormats {
    extern const PixelFormatDescriptor RGBA16_Premultiplied;
    extern const PixelFormatDescriptor RGBA8_Straight;
    extern const PixelFormatDescriptor RGB565_LE;
    extern const PixelFormatDescriptor RGB565_BE;
    extern const PixelFormatDescriptor RGB332;
    extern const PixelFormatDescriptor RGB888;
    extern const PixelFormatDescriptor BGR888;
    extern const PixelFormatDescriptor Gray8;   // 追加
    extern const PixelFormatDescriptor Gray16;  // 追加
}

namespace PixelFormatIDs {
    // ... 既存 ...
    inline const PixelFormatID Gray8  = &BuiltinFormats::Gray8;
    inline const PixelFormatID Gray16 = &BuiltinFormats::Gray16;
}

// builtinFormats 配列に追加
inline const PixelFormatID builtinFormats[] = {
    PixelFormatIDs::RGBA16_Premultiplied,
    PixelFormatIDs::RGBA8_Straight,
    PixelFormatIDs::RGB565_LE,
    PixelFormatIDs::RGB565_BE,
    PixelFormatIDs::RGB332,
    PixelFormatIDs::RGB888,
    PixelFormatIDs::BGR888,
    PixelFormatIDs::Gray8,   // 追加
    PixelFormatIDs::Gray16,  // 追加
};
```

### メリット

- ✅ **最小の変更量**：既存のインフラをそのまま利用
- ✅ **即座に使用可能**：AlphaMerge/SplitNodeを即座に実装できる
- ✅ **後方互換性**：既存コードへの影響ゼロ
- ✅ **実装リスクが低い**：既存の変換メカニズムをそのまま利用

### デメリット

- ⚠️ **メモリ効率**: RGBA8経由で4倍のメモリコピーが発生
- ⚠️ **CPU負荷**: 1チャンネル→4チャンネル→1チャンネルの変換オーバーヘッド

### パフォーマンス試算

```
512×512 Gray8画像の変換コスト (Gray8 → RGBA8 → Gray8):

メモリコピー:
- 入力読み込み: 262,144 bytes
- 中間バッファ: 1,048,576 bytes (×4)
- 出力書き込み: 262,144 bytes
- 合計: 1,572,864 bytes

CPU演算:
- toStandard: 262,144回のループ（R=G=B=gray代入）
- fromStandard: 262,144回のループ（輝度計算）
```

**評価**: AlphaMerge/SplitNodeの初期実装には許容範囲。ボトルネックになったら案Bで最適化。

---

## 案B: 専用変換経路の追加（パフォーマンス最適化）

### 設計方針

- グレースケール専用の変換経路を追加
- RGBA8経由の変換と並行して、Gray8専用の高速パスを提供
- 既存の構造体に最小限のフィールド追加

### 実装

#### PixelFormatDescriptor拡張

```cpp
struct PixelFormatDescriptor {
    const char* name;

    // ... 既存フィールド ...

    // グレースケール変換関数（オプション）
    using ToGrayFunc = void(*)(const void* src, uint8_t* dst, int pixelCount);
    using FromGrayFunc = void(*)(const uint8_t* src, void* dst, int pixelCount);

    ToGrayFunc toGray;      // このフォーマット → Gray8 直接変換（オプション）
    FromGrayFunc fromGray;  // Gray8 → このフォーマット 直接変換（オプション）
};
```

#### 変換ロジック拡張

```cpp
// pixel_format.h の convertFormat() を拡張

inline void convertFormat(const void* src, PixelFormatID srcFormat,
                          void* dst, PixelFormatID dstFormat,
                          int pixelCount,
                          const uint16_t* srcPalette = nullptr,
                          const uint16_t* dstPalette = nullptr) {
    // 1. 同一フォーマット: 単純コピー
    if (srcFormat == dstFormat) {
        // ... 既存実装 ...
        return;
    }

    // 2. 直接変換（RGBA16 ↔ RGBA8 等）
    DirectConvertFunc directFunc = getDirectConversion(srcFormat, dstFormat);
    if (directFunc) {
        directFunc(src, dst, pixelCount);
        return;
    }

    // 3. グレースケール最適化パス（新規追加）
    if (srcFormat->toGray && dstFormat->fromGray) {
        thread_local std::vector<uint8_t> grayBuffer;
        grayBuffer.resize(pixelCount);  // 1byte/pixel（4倍削減）
        srcFormat->toGray(src, grayBuffer.data(), pixelCount);
        dstFormat->fromGray(grayBuffer.data(), dst, pixelCount);
        return;
    }

    // 4. 標準フォーマット（RGBA8_Straight）経由
    if (!srcFormat || !dstFormat) return;
    thread_local std::vector<uint8_t> conversionBuffer;
    conversionBuffer.resize(pixelCount * 4);  // 4byte/pixel
    // ... 既存実装 ...
}
```

#### 各フォーマットにグレースケール変換を追加

```cpp
// RGBA8_Straight の例
static void rgba8Straight_toGray(const void* src, uint8_t* dst, int pixelCount) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        uint16_t r = s[i*4 + 0];
        uint16_t g = s[i*4 + 1];
        uint16_t b = s[i*4 + 2];
        dst[i] = static_cast<uint8_t>((77*r + 150*g + 29*b) >> 8);
    }
}

static void rgba8Straight_fromGray(const uint8_t* src, void* dst, int pixelCount) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t gray = src[i];
        d[i*4 + 0] = gray;
        d[i*4 + 1] = gray;
        d[i*4 + 2] = gray;
        d[i*4 + 3] = 255;
    }
}

// フォーマット定義に追加
const PixelFormatDescriptor RGBA8_Straight = {
    "RGBA8_Straight",
    // ... 既存フィールド ...
    rgba8Straight_toStandard,
    rgba8Straight_fromStandard,
    nullptr, nullptr,
    rgba8Straight_toGray,    // 追加
    rgba8Straight_fromGray   // 追加
};
```

### メリット

- ✅ **メモリ効率向上**: 中間バッファが1byte/pixelに削減（75%削減）
- ✅ **拡張性**: 将来的にYUV等の他の色空間も同様に追加可能
- ✅ **後方互換性**: 既存コードは引き続きRGBA8経由の変換を使用
- ✅ **段階的導入**: 必要なフォーマットにのみ変換関数を追加

### デメリット

- ⚠️ **実装コスト**: 全フォーマットに変換関数追加（オプショナルだが）
- ⚠️ **構造体拡張**: `PixelFormatDescriptor` にフィールド追加

### パフォーマンス改善試算

```
512×512 Gray8画像の変換コスト (Gray8 → RGBA8 → Gray8):

案A（RGBA8経由）:
- 中間バッファ: 1,048,576 bytes
- 合計メモリコピー: 1,572,864 bytes

案B（Gray8経由）:
- 中間バッファ: 262,144 bytes (75%削減)
- 合計メモリコピー: 786,432 bytes (50%削減)
```

---

## 案C: チャンネルモデルの型システム化（将来的な大規模拡張）

### 設計方針

チャンネル構造を型として明示化し、変換マトリクスを体系化。

### 実装概要

```cpp
enum class ChannelModel {
    Grayscale,  // 1チャンネル Y
    Alpha,      // 1チャンネル A
    RGB,        // 3チャンネル RGB
    RGBA,       // 4チャンネル RGBA
    YUV,        // 3チャンネル YUV（将来）
    CMYK,       // 4チャンネル CMYK（将来）
};

struct PixelFormatDescriptor {
    const char* name;
    ChannelModel channelModel;  // 新規追加

    uint8_t channelCount() const {
        switch (channelModel) {
            case ChannelModel::Grayscale:
            case ChannelModel::Alpha:
                return 1;
            case ChannelModel::RGB:
            case ChannelModel::YUV:
                return 3;
            case ChannelModel::RGBA:
            case ChannelModel::CMYK:
                return 4;
        }
    }

    // 変換関数は (ChannelModel, ChannelModel) のマトリクスで管理
    // ...
};
```

### メリット

- ✅ **型安全性**: チャンネル構造をコンパイル時に検証可能
- ✅ **拡張性**: YUV, LAB, CMYK等の追加が体系的に可能
- ✅ **意味的明確性**: フォーマットの意図が明確

### デメリット

- ❌ **大規模リファクタリング**: 既存コード全体に影響
- ❌ **変換マトリクスの爆発**: N×N通りの変換関数が必要
- ❌ **後方互換性の喪失**: 既存APIの変更が必要

### 評価

現時点では**過剰な設計**。YUV等の多様な色空間が必要になった段階で検討すべき。

---

## 推奨実装戦略：段階的アプローチ

### **Phase 1: Gray8 + Alpha8 基本実装（案A）**

**タイミング**: AlphaMerge/SplitNode実装の前提条件

**実装内容**:
1. `Gray8` フォーマット追加（輝度値）
   - `gray8_toStandard()` / `gray8_fromStandard()`
   - ITU-R BT.601 輝度計算実装
   - `hasAlpha = false`

2. `Alpha8` フォーマット追加（透明度）
   - `alpha8_toStandard()` / `alpha8_fromStandard()`
   - Aチャンネル抽出/注入実装
   - `hasAlpha = true`

3. Gray8 ⇔ Alpha8 直接変換の許可（警告付き）

4. `builtinFormats` に両方登録

**成果物**:
- AlphaMerge/SplitNodeが型安全に実装可能
- 意味的に正しいフォーマット使用
- メモリ効率は妥協（RGBA8経由、後で改善可能）

**工数**: 1-2日

---

### **Phase 2: パフォーマンス最適化（案B）**

**タイミング**: プロファイリングでボトルネックが確認された場合

**実装内容**:
1. `PixelFormatDescriptor` に `toGray` / `fromGray` 追加
2. `convertFormat()` にグレースケール最適化パス追加
3. よく使うフォーマット（RGBA8, RGBA16, RGB888等）に変換関数実装

**成果物**:
- Gray8変換が75%高速化
- メモリ使用量50%削減

**工数**: 1-2日

---

### **Phase 3: 将来的な拡張検討（案C）**

**タイミング**: YUV/LAB/CMYK等の多様な色空間が必要になった場合

**実装内容**:
- チャンネルモデルの型システム化
- 変換マトリクスの再設計

**工数**: 1-2週間（大規模リファクタリング）

---

## 考慮事項

### 1. 輝度変換の方式選択

グレースケール変換には複数の方式が存在：

| 方式 | 計算式 | 用途 |
|------|--------|------|
| ITU-R BT.601 | Y = 0.299R + 0.587G + 0.114B | 標準（SD画質） |
| ITU-R BT.709 | Y = 0.2126R + 0.7152G + 0.0722B | HD画質 |
| 平均値 | Y = (R + G + B) / 3 | 高速だが色味が変わる |
| Rチャンネル抽出 | Y = R | AlphaMerge用途（最速） |

**提案**:
- デフォルト: ITU-R BT.601（広く使われている）
- AlphaMerge/Split: Rチャンネル抽出（アルファ値として使う場合は変換不要）

### 2. Gray8 vs Alpha8 の区別（設計確定）

**採用方針**: 別のPixelFormatDescriptorとして定義

```cpp
// Gray8: 輝度値
const PixelFormatDescriptor Gray8 = {
    "Gray8",
    // ...
    false,  // hasAlpha
    // ...
    gray8_toStandard,      // 輝度展開
    gray8_fromStandard,    // 輝度計算
};

// Alpha8: 透明度
const PixelFormatDescriptor Alpha8 = {
    "Alpha8",
    // ...
    true,   // hasAlpha ← 重要な違い
    // ...
    alpha8_toStandard,     // アルファ展開
    alpha8_fromStandard,   // Aチャンネル抽出
};
```

**理由**:
- データの実体は同じ（1byte/pixel）
- 意味と変換関数が異なる
- ユーザーが制御権を持てる（Gray8 ⇔ Alpha8 変換も可能）
- グリーンバック→アルファ等の柔軟な操作をサポート

### 3. Premultiplied形式のグレースケール

Alpha値としてGray8を使う場合、Premultipliedの概念は不要。
RGB画像のグレースケール変換では、アルファは常に255（不透明）。

### 4. エンディアン

Gray8は1byteなのでエンディアン無関係。
Gray16では以下を検討：

```cpp
extern const PixelFormatDescriptor Gray16_LE;  // Little Endian
extern const PixelFormatDescriptor Gray16_BE;  // Big Endian
```

**推奨**: Phase 1では `Gray16` のみ（Native endian）。必要に応じて拡張。

---

## 実装順序

### ステップ1: Gray8フォーマット追加（Phase 1-1）

1. `pixel_format.cpp` に変換関数実装
   - `gray8_toStandard()` - 輝度展開
   - `gray8_fromStandard()` - ITU-R BT.601 輝度計算

2. `BuiltinFormats::Gray8` 定義
   - `hasAlpha = false`

3. `pixel_format.h` にエクスポート
   - `extern const PixelFormatDescriptor Gray8`
   - `PixelFormatIDs::Gray8`
   - `builtinFormats` 配列に追加

4. テストケース作成
   - Gray8 ↔ RGBA8 変換の正確性
   - 輝度計算の検証（ITU-R BT.601）
   - 境界値テスト（0, 128, 255）

### ステップ2: Alpha8フォーマット追加（Phase 1-2）

1. `pixel_format.cpp` に変換関数実装
   - `alpha8_toStandard()` - アルファ展開（可視化用）
   - `alpha8_fromStandard()` - Aチャンネル抽出

2. `BuiltinFormats::Alpha8` 定義
   - `hasAlpha = true`

3. `pixel_format.h` にエクスポート
   - `extern const PixelFormatDescriptor Alpha8`
   - `PixelFormatIDs::Alpha8`
   - `builtinFormats` 配列に追加

4. Gray8 ⇔ Alpha8 直接変換の実装（convertFormat内）

5. テストケース作成
   - Alpha8 ↔ RGBA8 変換の正確性
   - Gray8 ⇔ Alpha8 相互変換テスト

### ステップ3: AlphaMerge/SplitNodeでの利用

1. AlphaSplitNode: RGBA → RGB (RGBA8) + **Alpha8** ← 意味的に正しい
2. AlphaMergeNode: RGB (RGBA8) + **Alpha8** → RGBA

### ステップ4: ChannelExtractNode実装（オプション、柔軟性のため）

1. `channel_extract_node.h/cpp` 作成
2. SourceChannel列挙型（R, G, B, A）
3. OutputFormat選択（Gray8 or Alpha8）
4. グリーンバック→アルファ抽出のサンプル実装
5. テスト追加

### ステップ5: Gray16/Alpha16追加（オプション）

1. `gray16_toStandard()` / `gray16_fromStandard()` 実装
2. `alpha16_toStandard()` / `alpha16_fromStandard()` 実装
3. `BuiltinFormats::Gray16`, `Alpha16` 定義
4. テスト追加

### ステップ6: パフォーマンス測定

1. AlphaMerge/Splitノードのプロファイリング
2. Gray8/Alpha8変換がボトルネックになっているか確認
3. 必要ならPhase 2（案B）へ移行

---

## テスト戦略

### 単体テスト

```cpp
// Gray8変換の正確性テスト
TEST_CASE("Gray8 conversion") {
    // 1. Pure white
    uint8_t white_gray[1] = {255};
    uint8_t white_rgba[4];
    gray8_toStandard(white_gray, white_rgba, 1);
    REQUIRE(white_rgba[0] == 255);  // R
    REQUIRE(white_rgba[1] == 255);  // G
    REQUIRE(white_rgba[2] == 255);  // B
    REQUIRE(white_rgba[3] == 255);  // A

    // 2. Pure black
    uint8_t black_gray[1] = {0};
    uint8_t black_rgba[4];
    gray8_toStandard(black_gray, black_rgba, 1);
    REQUIRE(black_rgba[0] == 0);
    REQUIRE(black_rgba[1] == 0);
    REQUIRE(black_rgba[2] == 0);
    REQUIRE(black_rgba[3] == 255);  // A is opaque

    // 3. Round-trip conversion
    uint8_t original[1] = {128};
    uint8_t rgba[4];
    uint8_t recovered[1];
    gray8_toStandard(original, rgba, 1);
    gray8_fromStandard(rgba, recovered, 1);
    REQUIRE(recovered[0] == 128);  // Lossless for gray input
}

// RGB → Gray変換の輝度計算テスト
TEST_CASE("RGB to Gray luminance") {
    // Pure red (ITU-R BT.601: 0.299R)
    uint8_t red_rgba[4] = {255, 0, 0, 255};
    uint8_t red_gray[1];
    gray8_fromStandard(red_rgba, red_gray, 1);
    REQUIRE(red_gray[0] == 76);  // 255 * 0.299 ≈ 76

    // Pure green (ITU-R BT.601: 0.587G)
    uint8_t green_rgba[4] = {0, 255, 0, 255};
    uint8_t green_gray[1];
    gray8_fromStandard(green_rgba, green_gray, 1);
    REQUIRE(green_gray[0] == 150);  // 255 * 0.587 ≈ 150
}

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
    REQUIRE(rgba_dst[3] == 200);  // A=200

    // 3. Round-trip
    uint8_t original_alpha[1] = {64};
    uint8_t temp_rgba[4];
    uint8_t recovered_alpha[1];
    alpha8_toStandard(original_alpha, temp_rgba, 1);
    alpha8_fromStandard(temp_rgba, recovered_alpha, 1);
    REQUIRE(recovered_alpha[0] == 64);  // Lossless
}

// Gray8 ⇔ Alpha8 相互変換テスト
TEST_CASE("Gray8 to Alpha8 conversion") {
    uint8_t gray[1] = {100};
    uint8_t alpha[1];

    // Gray8 → Alpha8（メモリレイアウトは同じ）
    convertFormat(gray, PixelFormatIDs::Gray8,
                  alpha, PixelFormatIDs::Alpha8,
                  1);
    REQUIRE(alpha[0] == 100);  // ピクセル値はそのまま

    // 逆変換も同様
    uint8_t gray2[1];
    convertFormat(alpha, PixelFormatIDs::Alpha8,
                  gray2, PixelFormatIDs::Gray8,
                  1);
    REQUIRE(gray2[0] == 100);
}
```

### 統合テスト

```cpp
// AlphaSplitNode → AlphaMergeNode ラウンドトリップ
TEST_CASE("Alpha split-merge round trip") {
    // 1. 入力RGBA画像作成
    ImageBuffer rgba_input = ...;

    // 2. AlphaSplitNode
    AlphaSplitNode splitNode;
    ImageBuffer rgb_output = ...;
    ImageBuffer alpha_output = ...;  // Alpha8フォーマット ← 意味的に正しい

    // 3. AlphaMergeNode
    AlphaMergeNode mergeNode;
    ImageBuffer rgba_recovered = ...;

    // 4. 入力と復元結果の比較
    REQUIRE(rgba_input == rgba_recovered);  // Lossless
}

// ChannelExtractNode統合テスト
TEST_CASE("Green channel to alpha extraction") {
    // グリーンバック画像
    ImageBuffer greenback = ...;  // RGBA8

    // GチャンネルをAlpha8として抽出
    ChannelExtractNode extractNode;
    extractNode.setSourceChannel(ChannelExtractNode::SourceChannel::Green);
    extractNode.setOutputFormat(ChannelExtractNode::OutputFormat::Alpha);

    ImageBuffer alpha = extractNode.process(greenback);

    // フォーマット検証
    REQUIRE(alpha.format() == PixelFormatIDs::Alpha8);

    // 値検証（元画像のGチャンネルと一致）
    for (int i = 0; i < pixelCount; i++) {
        REQUIRE(alpha.pixels()[i] == greenback.pixels()[i*4 + 1]);  // G channel
    }
}
```

---

## 関連

- **IDEA_ALPHA_MERGE_SPLIT_NODES.md**: Gray8/Alpha8フォーマットの主要ユースケース
- **IDEA_INDEXED_COLOR_FORMATS.md**: 別の1byte/pixelフォーマット（Index8）
- **IDEA_FORMAT_NEGOTIATION.md**: フォーマット変換最適化の将来的方向性
- **pixel_format.h/cpp**: 実装対象ファイル
- **GrayscaleNode**: 既存のグレースケール変換ノード（RGB→RGB、フィルタとして動作）
- **ChannelExtractNode**: 任意チャンネル抽出ノード（本提案で追加）

---

## 実装例（Phase 1完全版）

### pixel_format.cpp への追加

```cpp
// ========================================================================
// Gray8: 8bit Grayscale
// ========================================================================

static void gray8_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t gray = s[i];
        dst[i*4 + 0] = gray;  // R
        dst[i*4 + 1] = gray;  // G
        dst[i*4 + 2] = gray;  // B
        dst[i*4 + 3] = 255;   // A (opaque)
    }
}

static void gray8_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        // ITU-R BT.601 luminance: Y = 0.299R + 0.587G + 0.114B
        // Integer version: Y = (77R + 150G + 29B) / 256
        uint16_t r = src[i*4 + 0];
        uint16_t g = src[i*4 + 1];
        uint16_t b = src[i*4 + 2];
        d[i] = static_cast<uint8_t>((77*r + 150*g + 29*b) >> 8);
    }
}

namespace BuiltinFormats {

// ... 既存フォーマット ...

const PixelFormatDescriptor Gray8 = {
    "Gray8",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    { ChannelDescriptor(8, 0),   // Gray in R position
      ChannelDescriptor(0, 0),   // No G
      ChannelDescriptor(0, 0),   // No B
      ChannelDescriptor(0, 0) }, // No A
    false,  // hasAlpha
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    gray8_toStandard,
    gray8_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr   // fromStandardIndexed
};

// ========================================================================
// Alpha8: 8bit Alpha Channel
// ========================================================================

static void alpha8_toStandard(const void* src, uint8_t* dst, int pixelCount) {
    const uint8_t* s = static_cast<const uint8_t*>(src);
    for (int i = 0; i < pixelCount; i++) {
        uint8_t alpha = s[i];
        // 可視化用: グレー表示
        dst[i*4 + 0] = alpha;  // R
        dst[i*4 + 1] = alpha;  // G
        dst[i*4 + 2] = alpha;  // B
        dst[i*4 + 3] = alpha;  // A

        // または黒+アルファ版（コメントアウト）:
        // dst[i*4 + 0] = 0;
        // dst[i*4 + 1] = 0;
        // dst[i*4 + 2] = 0;
        // dst[i*4 + 3] = alpha;
    }
}

static void alpha8_fromStandard(const uint8_t* src, void* dst, int pixelCount) {
    uint8_t* d = static_cast<uint8_t*>(dst);
    for (int i = 0; i < pixelCount; i++) {
        // Aチャンネルのみ抽出
        d[i] = src[i*4 + 3];
    }
}

const PixelFormatDescriptor Alpha8 = {
    "Alpha8",
    8,   // bitsPerPixel
    1,   // pixelsPerUnit
    1,   // bytesPerUnit
    { ChannelDescriptor(8, 0),   // Alpha value
      ChannelDescriptor(0, 0),   // No G
      ChannelDescriptor(0, 0),   // No B
      ChannelDescriptor(0, 0) }, // No A (data is alpha itself)
    true,   // hasAlpha ← 重要な違い！
    false,  // isPremultiplied
    false,  // isIndexed
    0,      // maxPaletteSize
    BitOrder::MSBFirst,
    ByteOrder::Native,
    alpha8_toStandard,
    alpha8_fromStandard,
    nullptr,  // toStandardIndexed
    nullptr   // fromStandardIndexed
};

} // namespace BuiltinFormats
```

### pixel_format.h への追加

```cpp
namespace BuiltinFormats {
    extern const PixelFormatDescriptor RGBA16_Premultiplied;
    extern const PixelFormatDescriptor RGBA8_Straight;
    extern const PixelFormatDescriptor RGB565_LE;
    extern const PixelFormatDescriptor RGB565_BE;
    extern const PixelFormatDescriptor RGB332;
    extern const PixelFormatDescriptor RGB888;
    extern const PixelFormatDescriptor BGR888;
    extern const PixelFormatDescriptor Gray8;   // 追加
}

namespace PixelFormatIDs {
    // 16bit RGBA系
    inline const PixelFormatID RGBA16_Premultiplied  = &BuiltinFormats::RGBA16_Premultiplied;

    // 8bit RGBA系
    inline const PixelFormatID RGBA8_Straight        = &BuiltinFormats::RGBA8_Straight;

    // パックドRGB系
    inline const PixelFormatID RGB565_LE             = &BuiltinFormats::RGB565_LE;
    inline const PixelFormatID RGB565_BE             = &BuiltinFormats::RGB565_BE;
    inline const PixelFormatID RGB332                = &BuiltinFormats::RGB332;

    // 24bit RGB系
    inline const PixelFormatID RGB888                = &BuiltinFormats::RGB888;
    inline const PixelFormatID BGR888                = &BuiltinFormats::BGR888;

    // グレースケール系
    inline const PixelFormatID Gray8                 = &BuiltinFormats::Gray8;  // 追加
}

// 組み込みフォーマット一覧（名前検索用）
inline const PixelFormatID builtinFormats[] = {
    PixelFormatIDs::RGBA16_Premultiplied,
    PixelFormatIDs::RGBA8_Straight,
    PixelFormatIDs::RGB565_LE,
    PixelFormatIDs::RGB565_BE,
    PixelFormatIDs::RGB332,
    PixelFormatIDs::RGB888,
    PixelFormatIDs::BGR888,
    PixelFormatIDs::Gray8,   // 追加
};
```

---

## 結論

**推奨**: Phase 1（案A）でGray8フォーマットを即座に実装し、AlphaMerge/SplitNodeを動作可能にする。パフォーマンスがボトルネックになった場合、Phase 2（案B）で最適化する段階的アプローチが最適。
