# フォーマット交渉（Format Negotiation）

## 概要

ノード間でピクセルフォーマットの要求を問い合わせ、パイプライン全体で最適なフォーマットを自動決定する仕組み。

## 動機

現状は「受け取った側が自分に適したフォーマットに変換する」方式だが、以下の非効率がある:

1. **冗長な変換**: A→B→C で、Aが出力をRGB565に変換 → Bが内部処理でRGBA16に変換 → Cが最終出力でRGB565に変換
2. **情報損失**: 高精度フォーマットから低精度に変換後、再度高精度が必要になるケース

## 現状の実装状況（2026-01）

### FormatConverter（実装済み）

フォーマット変換の事前解決は `FormatConverter` として実装完了:

```cpp
// pixel_format.h
struct FormatConverter {
    using ConvertFunc = void(*)(void* dst, const void* src, int pixelCount, const void* ctx);
    ConvertFunc func = nullptr;
    Context ctx;  // パレット、変換関数ポインタ等を保持

    void operator()(void* dst, const void* src, int pixelCount) const;
};

// 変換パスを事前解決
FormatConverter resolveConverter(PixelFormatID src, PixelFormatID dst,
                                  const PixelAuxInfo* aux, IAllocator* alloc);
```

**解決される変換パス:**
- 同一フォーマット → memcpy
- エンディアン兄弟 → swapEndian
- インデックスカラー → パレット展開（直接 or Straight経由）
- 一般 → toStraight/fromStraight の2段階

### toFormat/convertFormat への外部注入（実装済み）

prepare 段階で解決した FormatConverter を process で再利用可能:

```cpp
// image_buffer.h
ImageBuffer toFormat(PixelFormatID target,
                     FormatConversion mode = FormatConversion::CopyIfNeeded,
                     core::memory::IAllocator* alloc = nullptr,
                     const FormatConverter* converter = nullptr) &&;

// node.h
ImageBuffer convertFormat(ImageBuffer&& buffer, PixelFormatID target,
                          FormatConversion mode = FormatConversion::CopyIfNeeded,
                          const FormatConverter* converter = nullptr);
```

**使用例:**
```cpp
// prepare
converter_ = resolveConverter(srcFormat, dstFormat, auxPtr, allocator());

// process
auto result = convertFormat(std::move(input), dstFormat,
                            FormatConversion::CopyIfNeeded, &converter_);
```

## 未解決の課題

### 動的フォーマット問題

CompositeNode 経由で複数の Source がある場合、タイル位置によって最適な変換パスが変わる:

```
┌─────────────┬─────────────┐
│  SourceA    │  SourceB    │
│  (RGB565)   │  (RGB332)   │
├─────────────┼─────────────┤
│  SourceC    │  SourceA+B  │
│  (RGBA8)    │  (混在)     │
└─────────────┴─────────────┘
```

**問題**: prepare 時点で単一のフォーマットに固定できない

**対応案:**

#### 案1: 全パス事前解決
```cpp
// prepare: 使用される可能性のある全フォーマットを収集
std::unordered_map<PixelFormatID, FormatConverter> converters_;
for (auto fmt : possibleInputFormats) {
    converters_[fmt] = resolveConverter(fmt, targetFormat, ...);
}

// process: ルックアップのみ
auto& converter = converters_[input.formatID];
```

#### 案2: 動的解決 + キャッシュ
```cpp
// process: 初回のみ解決、以降はキャッシュ
auto it = converterCache_.find(input.formatID);
if (it == converterCache_.end()) {
    it = converterCache_.emplace(input.formatID,
        resolveConverter(input.formatID, targetFormat, ...)).first;
}
it->second(dst, src, width);
```

**検討ポイント:**
- 案1: オーバーヘッドは prepare に集中、process はシンプル
- 案2: 柔軟だが process 内に分岐が残る
- フォーマットの種類は限られている（数十程度）ので、案1 のコストは許容範囲か

### prepare の2フェーズ化

フォーマット情報の流れを整理するため、prepare を2段階に分ける案:

```
Phase1: 情報収集
  pushPreparePhase1 → 下流の capabilities・希望フォーマット・AABB を収集
  pullPreparePhase1 → 上流の capabilities・出力フォーマット・AABB を収集

Phase2: 確定通知
  pushPreparePhase2 → 確定情報（入力フォーマット等）を下流に通知
  pullPreparePhase2 → 確定情報（出力要求等）を上流に通知（必要に応じて）
```

**現状の問題:**
- pushPrepare は「準備して」と言いながら、準備に必要な情報を持っていない
- フォーマット情報が双方向に流れるが、確定通知の仕組みがない

詳細は IDEA_PREPARE_RESULT.md の「Phase 6」セクションを参照。

## 将来の拡張案

### possibleOutputFormats の収集

PrepareResponse に「出力しうるフォーマット群」を追加:

```cpp
struct PrepareResponse {
    // ... 既存フィールド ...
    std::vector<PixelFormatID> possibleOutputFormats;  // 出力可能なフォーマット群
};
```

Phase1 で収集し、RendererNode が全組み合わせの FormatConverter を事前解決。

### FormatPreference（将来）

より詳細な交渉のための構造体:

```cpp
struct FormatPreference {
    PixelFormatID preferred;                    // 最も望ましいフォーマット
    std::vector<PixelFormatID> acceptable;      // 許容可能なフォーマット
    bool canConvert;                            // 自前で変換可能か
};
```

## 関連ドキュメント

- IDEA_PREPARE_RESULT.md - prepare の統一改修計画
- CHANGELOG.md v2.57.0 - FormatConverter 実装の詳細
