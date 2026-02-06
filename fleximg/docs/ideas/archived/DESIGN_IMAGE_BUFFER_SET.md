# ImageBufferSet 設計

## 概要

ImageBufferSet は、複数の ImageBuffer を重複なく保持し、効率的な合成・変換を可能にするクラスです。

従来のパイプラインでは RenderResponse が単一の ImageBuffer を返していましたが、ImageBufferSet を導入することで：

- 重複がない領域は無変換のまま保持
- 重複がある場合のみ必要最小限の合成処理を実行
- フォーマット混在による柔軟な最適化が可能

```
従来:
  Layer1 ──┬──→ 合成 ──→ 単一バッファ ──→ Sink
  Layer2 ──┘

新方式:
  Layer1 ──┬──→ ImageBufferSet ──→ Sink
  Layer2 ──┘     (重複部分のみ合成、他は無変換保持)
```

## 背景・動機

### 現状の課題

1. **不要なフォーマット変換**: 重複がない場合でも全レイヤーをRGBA8_Straightに変換
2. **不要なmemset**: 描画しない領域のゼロクリアが必要
3. **3分割処理の複雑さ**: CompositeNodeで「描画済み/未描画」を管理するロジックが複雑

### 解決アプローチ

- 「重複がない範囲の配列」として複数バッファを保持
- 登録時に重複があれば即座に合成（z-order管理不要）
- 最終出力時（SinkNode）まで統合を先送り可能

## 設計方針

### 重複禁止の原則

ImageBufferSet 内のバッファは **絶対に重複しない** ことを保証します。

```
OK: [====]    [====]    [====]   （ギャップあり、重複なし）
OK: [====][====][====]           （隣接、重複なし）
NG: [====]                       （重複発生時は即座に合成）
        [====]
```

### 登録時の合成ルール

1. **重複なし**: そのままエントリとして追加
2. **重複あり・同一フォーマット**: 無変換で合成
3. **重複あり・異なるフォーマット**: RGBA8_Straight に変換して合成

### 統合タイミング

| 状況 | 動作 |
|------|------|
| 基本 | 先送り（SinkNodeで統合） |
| 半端なギャップ（数ピクセル） | 隣接バッファと統合 |
| フォーマット変換時 | 隣接領域を統合可能 |
| バッファ数上限超過 | 隣接を統合して数を削減 |

### 座標管理

**整数ピクセル単位で管理** します（CompositeNodeとの整合性）。

#### 調査結果

パイプラインにおける座標の流れ：

```
RendererNode.pivot (float)
    ↓ float_to_fixed
RenderRequest.origin (int_fixed, 端数あり)
    ↓ SourceNode
RenderResponse.origin (int_fixed, 端数継承)
    ↓ CompositeNode
from_fixed(origin.x - canvasOriginX) → 整数ピクセル単位で合成
```

- `RenderResponse.origin` は固定小数点で端数を保持
- `CompositeNode` では `from_fixed()`（= from_fixed_floor）で**端数を切り捨て**
- 合成処理は整数ピクセル単位で実行

#### ImageBufferSetでの方針

```cpp
struct Entry {
    ImageBuffer buffer;
    DataRange range;     // int16_t startX, endX（整数ピクセル単位）
};
```

- CompositeNodeと同様に整数ピクセル単位で管理
- 端数成分は合成時に切り捨て（from_fixed_floor）
- シンプルで重複判定が容易

## インターフェース

```cpp
class ImageBufferSet {
public:
    // ========================================
    // 構築・破棄
    // ========================================

    /// @brief コンストラクタ
    /// @param allocator メモリアロケータ（外部から渡す）
    explicit ImageBufferSet(IAllocator* allocator);

    /// @brief デストラクタ（保持バッファを解放）
    ~ImageBufferSet();

    // ========================================
    // バッファ登録
    // ========================================

    /// @brief バッファを登録
    /// @param buffer 追加するバッファ
    /// @param startX バッファの開始X座標（ワールド座標）
    /// @return 成功時 true
    /// @note 重複があれば即座に合成処理を実行
    bool addBuffer(const ImageBuffer& buffer, int16_t startX);

    /// @brief バッファを登録（DataRange指定）
    /// @param buffer 追加するバッファ
    /// @param range バッファの範囲
    bool addBuffer(const ImageBuffer& buffer, const DataRange& range);

    // ========================================
    // 変換・統合
    // ========================================

    /// @brief 全体を指定フォーマットに変換
    /// @param format 変換先フォーマット
    /// @param mergeAdjacent 隣接バッファを統合するか
    void convertFormat(PixelFormatID format, bool mergeAdjacent = false);

    /// @brief 単一バッファに統合して返す
    /// @param format 出力フォーマット（デフォルト: RGBA8_Straight）
    /// @return 統合されたバッファ
    ImageBuffer consolidate(PixelFormatID format = PixelFormatIDs::RGBA8_Straight);

    /// @brief 隣接バッファを統合（ギャップが閾値以下の場合）
    /// @param gapThreshold ギャップ閾値（ピクセル単位）
    void mergeAdjacent(int16_t gapThreshold = 8);

    // ========================================
    // アクセス
    // ========================================

    /// @brief バッファ数を取得
    int bufferCount() const;

    /// @brief 指定インデックスのバッファを取得
    const ImageBuffer& buffer(int index) const;

    /// @brief 指定インデックスの範囲を取得
    DataRange range(int index) const;

    /// @brief 全体の範囲（最小startX〜最大endX）を取得
    DataRange totalRange() const;

    /// @brief 空かどうか
    bool empty() const;

    // ========================================
    // 状態管理
    // ========================================

    /// @brief クリア（再利用時）
    void clear();

    /// @brief アロケータを設定
    void setAllocator(IAllocator* allocator);

private:
    // 内部構造（後述）
};
```

## 内部構造

### エントリ構造

```cpp
struct Entry {
    ImageBuffer buffer;   // バッファ本体
    DataRange range;      // 範囲（startX, endX）
    // 注: Y座標は現時点では同一Y座標を前提（将来拡張で追加可能）
};
```

### メンバ変数

```cpp
class ImageBufferSet {
private:
    static constexpr int MAX_ENTRIES = 8;  // 組み込み向け固定上限

    Entry entries_[MAX_ENTRIES];  // エントリ配列（ソート済み）
    int entryCount_ = 0;          // 有効エントリ数
    IAllocator* allocator_;       // メモリアロケータ
};
```

### ソート済み配列の維持

エントリは常に `startX` の昇順でソートされた状態を維持します。

```
entries_[0].range.startX < entries_[1].range.startX < entries_[2].range.startX ...
```

これにより：
- 重複判定が O(log N) または O(N) で可能
- 隣接判定が容易
- イテレーション時に左から右へ順序保証

## 処理フロー

### addBuffer フロー

```
addBuffer(buffer, startX)
    │
    ├─→ 重複チェック（既存エントリと比較）
    │     │
    │     ├─ 重複なし ─→ 新エントリとして追加（ソート位置に挿入）
    │     │
    │     └─ 重複あり ─→ 合成処理へ
    │           │
    │           ├─ 同一フォーマット ─→ 無変換合成
    │           │
    │           └─ 異なるフォーマット ─→ RGBA8_Straight変換 → 合成
    │
    └─→ エントリ数チェック
          │
          └─ MAX_ENTRIES超過 ─→ mergeAdjacent() で統合
```

### 重複合成の詳細

```
既存:     [=====A=====]
新規:           [=====B=====]
              ↓
分割:     [==A==][overlap][==B==]
              ↓
処理:     A部分: そのまま保持
          overlap: A と B を合成（under合成）
          B部分: そのまま保持
              ↓
結果:     [==A==][merged][==B==]
          または
          [========merged========]  ← 隣接なら統合
```

### consolidate フロー

```
consolidate(format)
    │
    ├─→ バッファ数 == 0 ─→ 空バッファを返す
    │
    ├─→ バッファ数 == 1 かつ format一致 ─→ そのまま返す
    │
    └─→ 複数バッファ または format不一致
          │
          ├─→ 全体範囲を計算
          │
          ├─→ 出力バッファを確保
          │
          ├─→ 各エントリを順に描画
          │     ├─ ギャップ ─→ ゼロクリア
          │     └─ バッファ ─→ フォーマット変換しながらコピー
          │
          └─→ 統合バッファを返す
```

## 使用例

### CompositeNode での使用

```cpp
RenderResponse CompositeNode::onPullProcess(const RenderRequest& request) {
    ImageBufferSet bufferSet(allocator());

    // 各レイヤーを登録（重複があれば自動で合成）
    for (int i = 0; i < validUpstreamCount_; i++) {
        RenderResponse result = upstreamCache_[i].node->pullProcess(request);
        if (result.isValid()) {
            int16_t startX = from_fixed(result.origin.x - canvasOriginX);
            bufferSet.addBuffer(result.buffer, startX);
        }
    }

    // 結果を返す（複数バッファのまま、または統合）
    // 案1: 複数バッファのまま返す（RenderResponse拡張が必要）
    // 案2: 統合して単一バッファで返す
    return RenderResponse{bufferSet.consolidate(), request.origin};
}
```

### SinkNode での使用

```cpp
void SinkNode::onPushProcess(RenderResponse&& input, const RenderRequest& request) {
    // 入力が ImageBufferSet を含む場合
    if (input.hasBufferSet()) {
        ImageBufferSet& set = input.bufferSet();

        // 出力フォーマットに統合
        ImageBuffer consolidated = set.consolidate(outputFormat_);

        // 出力バッファに描画
        copyToOutput(consolidated);
    } else {
        // 従来の単一バッファ処理
        copyToOutput(input.buffer);
    }
}
```

## RenderResponse との統合

### 案1: RenderResponse に ImageBufferSet を埋め込む

```cpp
struct RenderResponse {
    // 既存
    ImageBuffer buffer;
    Point origin;

    // 追加
    ImageBufferSet* bufferSet = nullptr;  // 複数バッファ時に使用

    bool hasBufferSet() const { return bufferSet != nullptr; }
};
```

### 案2: 別の Response 型を用意

```cpp
struct MultiBufferResponse {
    ImageBufferSet bufferSet;
    Point origin;
    int16_t canvasWidth;
};
```

### 案3: ImageBufferSet を RenderResponse の代替として使用

```cpp
// ImageBufferSet 自体が origin 情報を持つ
class ImageBufferSet {
    Point origin_;  // 全体の基準点
    // ...
};
```

## 閾値とパラメータ

| パラメータ | デフォルト値 | 説明 |
|-----------|-------------|------|
| MAX_ENTRIES | 8 | 最大エントリ数（組み込み向け） |
| gapThreshold | 8 | 隣接統合のギャップ閾値（ピクセル） |
| mergeOnExceed | true | MAX_ENTRIES超過時に自動統合するか |

## 今後の拡張

### 2D領域対応

現時点では同一Y座標（1行単位）を前提としていますが、将来的には矩形領域に拡張可能：

```cpp
struct Entry {
    ImageBuffer buffer;
    Rect region;  // x, y, width, height
};
```

### 動的エントリ配列

組み込み環境以外では、動的配列での実装も検討：

```cpp
#ifdef FLEXIMG_DYNAMIC_ALLOCATION
    std::vector<Entry> entries_;
#else
    Entry entries_[MAX_ENTRIES];
#endif
```

## 関連ドキュメント

- [ARCHITECTURE.md](ARCHITECTURE.md) - 全体アーキテクチャ
- [DESIGN_RENDERER_NODE.md](DESIGN_RENDERER_NODE.md) - RendererNode 設計
- [DESIGN_PIXEL_FORMAT.md](DESIGN_PIXEL_FORMAT.md) - ピクセルフォーマット設計
