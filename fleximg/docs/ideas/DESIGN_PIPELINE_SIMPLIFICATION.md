# パイプラインデータ取り回し簡素化 実装計画

## 概要

**目的**: パイプライン上のデータ取り回しを簡素化・高速化
**方針**: 安全管理を使用者側責務にし、ゼロオーバーヘッド原則を適用

**ブランチ**: `claude/analyze-fleximg-SfVQg`
**ベースコミット**: `7b3e569`

---

## フェーズ構成

```
Phase 1: 基盤整備（破壊的変更の準備）    ✅ 完了
Phase 2: RenderResponse 単純化           ✅ 完了
Phase 3: ImageBuffer 最適化              （未着手）
Phase 4: フォーマット変換の事前解決      ✅ 既存で実装済み（FormatConverter）
Phase 5: ImageBufferSet 簡素化           （未着手）
Phase 6: ノード実装の更新                ✅ Phase 2で完了
```

---

## Phase 1: 基盤整備

### 1.1 assert マクロの統一

**ファイル**: `src/fleximg/core/common.h`

```cpp
// デバッグ時のみ有効な assert
#ifdef FLEXIMG_DEBUG
  #define FLEXIMG_ASSERT(cond, msg) \
    do { if (!(cond)) { FLEXIMG_DEBUG_LOG("ASSERT: " msg); std::abort(); } } while(0)
#else
  #define FLEXIMG_ASSERT(cond, msg) ((void)0)
#endif

// 常に有効な assert（致命的エラー用）
#define FLEXIMG_REQUIRE(cond, msg) \
  do { if (!(cond)) { std::abort(); } } while(0)
```

### 1.2 既存 assert の置き換え ✅ 完了

| ファイル | 変更 | 理由 |
|----------|------|------|
| `image_buffer.h` | `assert` → `FLEXIMG_REQUIRE` | メモリ確保失敗は致命的 |
| `viewport.h` | `assert` → `FLEXIMG_ASSERT` | フォーマット不一致はデバッグ用 |
| `node.h` (2箇所) | `assert` → `FLEXIMG_ASSERT` | スキャンライン高さチェック |

`allocator.h` のトラップ用 assert はバックトレース取得目的のため維持。

### 1.3 deprecated マクロの追加

**ファイル**: `src/fleximg/core/common.h`

```cpp
#if __cplusplus >= 201402L
  #define FLEXIMG_DEPRECATED(msg) [[deprecated(msg)]]
#else
  #define FLEXIMG_DEPRECATED(msg)
#endif
```

### 1.3 テスト基盤の確認

```bash
cd fleximg/test
make all_tests && ./all_tests
```

**完了条件**: 全テスト PASS

---

## Phase 2: RenderResponse 単純化

### 2.1 レガシー buffer メンバの削除

**ファイル**: `src/fleximg/image/render_types.h`

**Before**:
```cpp
struct RenderResponse {
    ImageBufferSet bufferSet;
    Point origin;
    ImageBuffer buffer;  // レガシー
    // ...
};
```

**After**:
```cpp
struct RenderResponse {
    ImageBufferSet bufferSet;
    Point origin;

    // コンストラクタ
    RenderResponse() = default;
    RenderResponse(ImageBufferSet&& set, Point org)
        : bufferSet(std::move(set)), origin(org) {}
    RenderResponse(ImageBuffer&& buf, Point org);

    // ムーブのみ
    RenderResponse(RenderResponse&&) = default;
    RenderResponse& operator=(RenderResponse&&) = default;
    RenderResponse(const RenderResponse&) = delete;
    RenderResponse& operator=(const RenderResponse&) = delete;

    // シンプルなアクセス
    bool empty() const { return bufferSet.empty(); }
    int bufferCount() const { return bufferSet.bufferCount(); }

    // 単一バッファアクセス（bufferCount()==1 前提）
    ImageBuffer& single() {
        FLEXIMG_ASSERT(bufferSet.bufferCount() == 1, "Expected single buffer");
        return bufferSet.buffer(0);
    }
    const ImageBuffer& single() const {
        FLEXIMG_ASSERT(bufferSet.bufferCount() == 1, "Expected single buffer");
        return bufferSet.buffer(0);
    }
    ViewPort singleView() { return single().view(); }
    ViewPort singleView() const { return single().view(); }
};
```

### 2.2 consolidateIfNeeded() の削除

**ファイル**: `src/fleximg/image/render_types.h`

- `consolidateIfNeeded()` 関数を削除
- 代わりに `ImageBufferSet::consolidate()` を直接使用

### 2.3 影響を受けるノードの更新

| ノード | ファイル | 変更内容 |
|--------|----------|----------|
| FilterNodeBase | filter_node_base.h | consolidateIfNeeded → bufferSet.consolidate() |
| DistributorNode | distributor_node.h | 同上 |
| SinkNode | sink_node.h | 同上 |
| MatteNode | matte_node.h | 同上（3箇所） |

**完了条件**: 全テスト PASS

---

## Phase 3: ImageBuffer 最適化

### 3.1 コピーコンストラクタの削除

**ファイル**: `src/fleximg/image/image_buffer.h`

**Before**:
```cpp
ImageBuffer(const ImageBuffer& other) { ... }  // 深コピー
ImageBuffer& operator=(const ImageBuffer& other) { ... }
```

**After**:
```cpp
// コピー禁止
ImageBuffer(const ImageBuffer&) = delete;
ImageBuffer& operator=(const ImageBuffer&) = delete;

// 明示的コピー
[[nodiscard]] ImageBuffer clone(IAllocator* alloc = nullptr) const {
    IAllocator* targetAlloc = alloc ? alloc : allocator_;
    if (!targetAlloc) {
        targetAlloc = &core::memory::DefaultAllocator::instance();
    }
    ImageBuffer copy(view_.width, view_.height, view_.formatID,
                     InitPolicy::Uninitialized, targetAlloc);
    if (copy.isValid() && isValid()) {
        copy.copyFrom(*this);
    }
    return copy;
}
```

### 3.2 参照モードファクトリの追加

```cpp
// 参照モードバッファの作成
[[nodiscard]] static ImageBuffer wrapView(const ViewPort& vp) {
    return ImageBuffer(vp);
}
```

### 3.3 ImageBufferSet::addBuffer(const&) の削除

**ファイル**: `src/fleximg/image/image_buffer_set.h`

```cpp
// 削除
bool addBuffer(const ImageBuffer& buffer, int16_t startX);

// 残す（ムーブのみ）
bool addBuffer(ImageBuffer&& buffer, int16_t startX);
```

### 3.4 コンパイルエラーの修正

コピーを使用している箇所を特定し、clone() または ムーブに変更

**完了条件**: 全テスト PASS

---

## Phase 4: フォーマット変換の事前解決

### 4.1 FormatConversionPath 構造体の追加

**ファイル**: `src/fleximg/image/pixel_format.h`（新規セクション）

```cpp
/// フォーマット変換パス（Prepare時に解決）
struct FormatConversionPath {
    enum class Type : uint8_t {
        None,           // 変換不要（同一フォーマット）
        Direct,         // 直接変換
        ViaStraight,    // RGBA8_Straight経由
        MemcpyOnly,     // memcpyのみ（同一フォーマット、異なるインスタンス）
        Unsupported     // 変換不可
    };

    Type type = Type::Unsupported;
    PixelConvertFunc convert = nullptr;      // Direct変換用
    PixelConvertFunc toStraight = nullptr;   // ViaStraight用（前半）
    PixelConvertFunc fromStraight = nullptr; // ViaStraight用（後半）
    uint8_t srcBpp = 0;
    uint8_t dstBpp = 0;

    bool isValid() const { return type != Type::Unsupported; }
};

/// フォーマット変換パスを解決
FormatConversionPath resolveConversionPath(PixelFormatID src, PixelFormatID dst);
```

### 4.2 resolveConversionPath 実装

**ファイル**: `src/fleximg/image/pixel_format.h`（FLEXIMG_IMPLEMENTATION セクション）

```cpp
inline FormatConversionPath resolveConversionPath(PixelFormatID src, PixelFormatID dst) {
    FormatConversionPath path;
    path.srcBpp = src->bytesPerPixel;
    path.dstBpp = dst->bytesPerPixel;

    // 同一フォーマット
    if (src == dst) {
        path.type = FormatConversionPath::Type::MemcpyOnly;
        return path;
    }

    // dst が RGBA8_Straight で src に toStraight がある
    if (dst == PixelFormatIDs::RGBA8_Straight && src->toStraight) {
        path.type = FormatConversionPath::Type::Direct;
        path.convert = src->toStraight;
        return path;
    }

    // src が RGBA8_Straight で dst に fromStraight がある
    if (src == PixelFormatIDs::RGBA8_Straight && dst->fromStraight) {
        path.type = FormatConversionPath::Type::Direct;
        path.convert = dst->fromStraight;
        return path;
    }

    // 両方向変換可能
    if (src->toStraight && dst->fromStraight) {
        path.type = FormatConversionPath::Type::ViaStraight;
        path.toStraight = src->toStraight;
        path.fromStraight = dst->fromStraight;
        return path;
    }

    // 変換不可
    path.type = FormatConversionPath::Type::Unsupported;
    return path;
}
```

### 4.3 高速変換関数の追加

```cpp
/// 事前解決済みパスで変換（チェックなし）
inline void convertWithPath(
    const FormatConversionPath& path,
    void* dst, const void* src, int32_t pixelCount,
    const void* palette = nullptr,
    void* tempBuffer = nullptr  // ViaStraight用、4*pixelCount bytes
) {
    switch (path.type) {
        case FormatConversionPath::Type::None:
        case FormatConversionPath::Type::MemcpyOnly:
            std::memcpy(dst, src, static_cast<size_t>(pixelCount) * path.srcBpp);
            break;
        case FormatConversionPath::Type::Direct:
            path.convert(dst, src, pixelCount, palette);
            break;
        case FormatConversionPath::Type::ViaStraight:
            path.toStraight(tempBuffer, src, pixelCount, palette);
            path.fromStraight(dst, tempBuffer, pixelCount, nullptr);
            break;
        case FormatConversionPath::Type::Unsupported:
            // 呼び出し側責任：到達しないはず
            break;
    }
}
```

### 4.4 ImageBufferSet での使用

**ファイル**: `src/fleximg/image/image_buffer_set.h`

`convertFormat()`, `mergeOverlapping()`, `consolidate()` で FormatConversionPath を使用

**完了条件**: 全テスト PASS、ベンチマークで改善確認

---

## Phase 5: ImageBufferSet 簡素化

### 5.1 addBufferUnchecked の追加

**ファイル**: `src/fleximg/image/image_buffer_set.h`

```cpp
/// オーバーラップチェックなしでバッファを追加
/// @pre プール枯渇していないこと
/// @pre オーバーラップがないこと（呼び出し側責任）
void addBufferUnchecked(ImageBuffer&& buf, int16_t startX) {
    Entry* entry = acquireEntry();
    FLEXIMG_ASSERT(entry, "Pool exhausted");
    entry->buffer = std::move(buf);
    entry->range.startX = startX;
    entry->range.endX = startX + entry->buffer.view().width;
    insertSorted(entry);
}
```

### 5.2 既存 addBuffer の簡素化

```cpp
/// バッファを追加（オーバーラップ時はマージ）
/// @return 成功時 true、プール枯渇時 false
bool addBuffer(ImageBuffer&& buf, int16_t startX) {
    // オーバーラップチェック
    if (hasOverlap(startX, startX + buf.view().width)) {
        return addBufferWithMerge(std::move(buf), startX);
    }

    Entry* entry = acquireEntry();
    if (!entry) return false;  // プール枯渇

    entry->buffer = std::move(buf);
    entry->range.startX = startX;
    entry->range.endX = startX + entry->buffer.view().width;
    insertSorted(entry);
    return true;
}
```

### 5.3 マージ時のフォーマット選択

```cpp
/// オーバーラップマージ（フォーマット指定可能）
/// @param targetFormat nullなら最初のエントリのフォーマットを使用
void mergeOverlapping(PixelFormatID targetFormat = nullptr);
```

**完了条件**: 全テスト PASS

---

## Phase 6: ノード実装の更新

### 6.1 アロケータチェックの削除

**対象ファイル**:
- `nodes/composite_node.h`
- `nodes/renderer_node.h`
- `nodes/sink_node.h`

**変更パターン**:
```cpp
// Before
if (allocator()) {
    void* mem = allocator()->allocate(size);
    if (mem) { ... }
}

// After
void* mem = allocator()->allocate(size);
FLEXIMG_ASSERT(mem, "Allocation failed");
// ...
```

### 6.2 RendererNode での事前検証

**ファイル**: `nodes/renderer_node.h`

```cpp
void exec() {
    // 事前条件の検証
    FLEXIMG_REQUIRE(pipelineAllocator_, "Allocator must be set");
    FLEXIMG_REQUIRE(virtualScreenWidth_ > 0 && virtualScreenHeight_ > 0,
                    "Virtual screen must be set");

    // 以降はチェックなし
    execPrepare();
    execProcess();
    execFinalize();
}
```

### 6.3 FilterNodeBase の更新

**ファイル**: `nodes/filter_node_base.h`

```cpp
RenderResponse process(RenderResponse&& input) {
    // 単一バッファ前提
    FLEXIMG_ASSERT(input.bufferCount() == 1, "Filter expects single buffer");

    ImageBuffer working = std::move(input.single());
    // フィルタ処理...

    return RenderResponse(std::move(working), input.origin);
}
```

### 6.4 CompositeNode の更新

- `bufferSet_` の直接操作
- `addBufferUnchecked` の使用（オーバーラップなしが保証される場合）

### 6.5 SinkNode の更新

- `consolidateIfNeeded` → 直接 `bufferSet.consolidate()` または `single()` 使用

**完了条件**: 全テスト PASS

---

## テスト計画

### 各フェーズ後のテスト

```bash
cd fleximg/test
make clean && make all_tests && ./all_tests
```

### ベンチマーク（Phase 4, 6 後）

```bash
cd fleximg
pio run -e bench_native && .pio/build/bench_native/program
```

### 破壊的変更の確認

- examples/ のビルド確認
- demo/ のビルド確認

---

## リスクと対策

| リスク | 対策 |
|--------|------|
| コピー禁止による既存コード破壊 | clone() への置換ガイド提供 |
| assert の本番無効化 | FLEXIMG_REQUIRE で致命的エラーは常に検出 |
| テスト不足 | 各フェーズで全テスト実行 |
| 後方互換性 | deprecated 警告を出しつつ段階的移行 |

---

## 作業見積り

| フェーズ | 内容 | ファイル数 |
|----------|------|------------|
| Phase 1 | 基盤整備 | 1 |
| Phase 2 | RenderResponse | 5 |
| Phase 3 | ImageBuffer | 3 |
| Phase 4 | フォーマット変換 | 2 |
| Phase 5 | ImageBufferSet | 1 |
| Phase 6 | ノード更新 | 6 |

---

## 次のアクション

1. Phase 1 から順に実装開始
2. 各フェーズ完了後にコミット
3. 全フェーズ完了後に PR 作成

---

## 影響ファイル詳細

### consolidateIfNeeded 使用箇所（12箇所）

**コアノード（7箇所）**:
| ファイル | 行 | コンテキスト |
|----------|-----|--------------|
| `core/node.h` | 384, 620 | 定義・実装 |
| `nodes/sink_node.h` | 211 | Push処理 |
| `nodes/filter_node_base.h` | 135 | Process |
| `nodes/horizontal_blur_node.h` | 229, 338 | Process |
| `nodes/distributor_node.h` | 207 | Push処理 |
| `nodes/vertical_blur_node.h` | 413, 645 | Process |
| `nodes/matte_node.h` | 355, 397, 479 | マスク/BG/FG処理 |

**examples（3箇所）**:
| ファイル | 行 |
|----------|-----|
| `examples/m5stack_basic/src/lcd_sink_node.h` | 107 |
| `examples/m5stack_hos/src/lcd_sink_node.h` | 107 |
| `examples/m5stack_matte/src/lcd_sink_node.h` | 107 |

### ImageBuffer コピー使用箇所

| ファイル | 行 | 内容 |
|----------|-----|------|
| `image/image_buffer.h` | 97 | コピーコンストラクタ定義 |
| `image/image_buffer_set.h` | 140, 311 | addBuffer(const&) |

### render_types.h の buffer メンバ参照

| ファイル | 行 | 内容 |
|----------|-----|------|
| `image/render_types.h` | 356-397 | buffer メンバとコメント |

---

## 更新履歴

- 2026-02-01: 初版作成
- 2026-02-01: 影響ファイル詳細を追加
