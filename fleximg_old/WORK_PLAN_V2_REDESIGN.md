# fleximg v2 設計刷新 作業計画書

## 概要

C++ネイティブ/Arduino環境での使いやすさを重視した新設計への移行。
現状のGraphNode/GraphConnection方式から、直感的なNode/Port接続方式へ刷新する。

## 背景と目的

### 現状の課題
- GraphNode構造体が多目的すぎる（全ノードタイプの属性を1構造体に）
- 文字列IDベースの参照（型安全性の欠如）
- `setNodes()` + `setConnections()` は冗長なAPI
- JS/WASM向けに最適化されており、C++ネイティブでは使いにくい
- ImageBuffer→ViewPort継承によるスライシングリスク

### 目標
- 直感的なAPI: `src >> transform >> sink`
- 型安全: オブジェクト参照による接続
- 組込み適性: メモリ管理は呼び出し側責務、最小限のオーバーヘッド
- 拡張性: カスタムRendererによるタイル戦略のカスタマイズ
- 安全性: ImageBufferのコンポジション化でスライシング防止

---

## 命名規則

### ノードクラス

| クラス名 | 役割 |
|----------|------|
| `Node` | 基底クラス |
| `SourceNode` | 画像入力（終端） |
| `SinkNode` | 画像出力（終端） |
| `FilterNode` | フィルタ処理 |
| `TransformNode` | アフィン変換 |
| `CompositeNode` | 複数入力合成 |

### 実行・データ型

| 型名 | 役割 |
|------|------|
| `Port` | 接続点（1:1接続） |
| `Renderer` | パイプライン実行者 |
| `TileConfig` | タイル分割設定 |
| `ViewPort` | 画像ビュー（軽量POD） |
| `ImageBuffer` | メモリ所有画像（コンポジション） |
| `RenderResult` | 評価結果 |

---

## 新設計の概要

### Node/Port モデル
```cpp
// 接続点（1:1接続）
struct Port {
    Node* owner = nullptr;
    Port* connected = nullptr;
    int index = 0;

    bool connect(Port& other);
    void disconnect();
};

// ノード基底
class Node {
protected:
    std::vector<Port> inputs_;
    std::vector<Port> outputs_;

public:
    // 詳細API
    Port* inputPort(int index = 0);
    Port* outputPort(int index = 0);

    // 簡易API
    bool connectTo(Node& target, int targetInput = 0, int output = 0);
    bool connectFrom(Node& source, int sourceOutput = 0, int input = 0);

    // 演算子
    Node& operator>>(Node& downstream);
    Node& operator<<(Node& upstream);
};
```

### ViewPort/ImageBuffer（コンポジション）
```cpp
// 純粋ビュー（軽量POD）
struct ViewPort {
    void* data;
    PixelFormatID formatID;
    size_t stride;
    int width, height;

    bool isValid() const;
    void* pixelAt(int x, int y) const;
};

// 操作はフリー関数
namespace view_ops {
    void blendFirst(ViewPort& dst, const ViewPort& src, int ox, int oy);
    void blendOnto(ViewPort& dst, const ViewPort& src, int ox, int oy);
    ViewPort subView(const ViewPort& v, int x, int y, int w, int h);
}

// メモリ所有（コンポジション、継承なし）
class ImageBuffer {
    void* data_ = nullptr;
    PixelFormatID formatID_;
    size_t stride_;
    int width_, height_;
    size_t capacity_ = 0;
    ImageAllocator* allocator_ = nullptr;

public:
    ViewPort view() const;
    ViewPort subView(int x, int y, int w, int h) const;
    void* pixelAt(int x, int y);
};
```

### Renderer
```cpp
class Renderer {
public:
    explicit Renderer(SinkNode& output);

    void setOrigin(float x, float y);
    void setTileConfig(const TileConfig& config);

    // 簡易API
    void exec() { prepare(); execute(); finalize(); }

    // 詳細API
    virtual void prepare();
    virtual void execute();
    virtual void finalize();

protected:
    // カスタマイズポイント
    virtual std::vector<TileRegion> computeTiles();
    virtual void processTile(const RenderContext& ctx);
};
```

### 処理アルゴリズム（純関数）
```cpp
namespace transform {
    void affine(ViewPort& dst, float dstOx, float dstOy,
                const ViewPort& src, float srcOx, float srcOy,
                const AffineMatrix& matrix);
}

namespace filters {
    void brightness(ViewPort& dst, const ViewPort& src, float amount);
    void grayscale(ViewPort& dst, const ViewPort& src);
    void boxBlur(ViewPort& dst, const ViewPort& src, int radius);
}

namespace blend {
    void first(ViewPort& dst, float dstOx, float dstOy,
               const ViewPort& src, float srcOx, float srcOy);
    void onto(ViewPort& dst, float dstOx, float dstOy,
              const ViewPort& src, float srcOx, float srcOy);
}
```

---

## ディレクトリ構成

```
fleximg_v2/
├── src/fleximg_v2/
│   │
│   │  # 現行から流用・改修
│   ├── common.h
│   ├── pixel_format.h
│   ├── pixel_format_registry.h
│   ├── pixel_format_registry.cpp
│   ├── image_allocator.h
│   ├── viewport.h              # 軽量POD化
│   ├── viewport.cpp            # → view_ops.cpp
│   ├── image_buffer.h          # コンポジション化
│   ├── image_buffer.cpp
│   │
│   │  # 新規作成
│   ├── port.h
│   ├── node.h
│   ├── node.cpp
│   ├── nodes/
│   │   ├── source_node.h
│   │   ├── sink_node.h
│   │   ├── filter_node.h
│   │   ├── transform_node.h
│   │   └── composite_node.h
│   ├── renderer.h
│   ├── renderer.cpp
│   ├── render_result.h
│   │
│   │  # 処理アルゴリズム（純関数）
│   └── operations/
│       ├── blend.h
│       ├── blend.cpp
│       ├── transform.h
│       ├── transform.cpp
│       ├── filters.h
│       └── filters.cpp
│
├── test/
│   ├── viewport_test.cpp
│   ├── image_buffer_test.cpp
│   ├── port_test.cpp
│   ├── node_test.cpp
│   ├── renderer_test.cpp
│   └── integration_test.cpp
│
├── Makefile
└── README.md
```

---

## 作業フェーズ

### Phase 1: 基盤構築
**目標**: ディレクトリ構成確立、流用ファイルのコピーと改修

1. `fleximg_v2/` ディレクトリ作成
2. 流用ファイルのコピー
   - common.h（namespace調整）
   - pixel_format.h, pixel_format_registry.h/cpp
   - image_allocator.h
3. ViewPort改修（軽量POD化）
4. ImageBuffer改修（コンポジション化）
5. ビルド確認用Makefile作成

**成果物**: コンパイル可能な基盤、改善されたViewPort/ImageBuffer

---

### Phase 2: Port/Node基底
**目標**: 接続モデルの実装

1. `port.h` - Port構造体
2. `node.h/cpp` - Node基底クラス
3. 単体テスト作成

**成果物**: 接続可能なNode基底クラス

---

### Phase 3: 基本ノード実装
**目標**: SourceNode/SinkNode の実装

1. `source_node.h` - 画像入力ノード
2. `sink_node.h` - 画像出力ノード
3. 接続テスト

**成果物**: 入出力ノードが接続可能

---

### Phase 4: Renderer基本実装
**目標**: 最小限のパイプライン実行

1. `render_result.h` - RenderResult構造体
2. `renderer.h/cpp` - Renderer
3. 画像コピー（source → sink）のみで動作確認
4. テスト作成

**成果物**: `src >> sink` でピクセルコピーが動作

---

### Phase 5: 処理オペレーション移植
**目標**: アルゴリズムを純関数として移植

1. `operations/blend.h/cpp`
2. `operations/transform.h/cpp`
3. `operations/filters.h/cpp`
4. 単体テスト

**成果物**: 独立した処理アルゴリズムモジュール

---

### Phase 6: 変換ノード実装
**目標**: TransformNode/FilterNode の実装

1. `transform_node.h`
2. `filter_node.h`
3. 統合テスト（`src >> transform >> sink`）

**成果物**: 変換パイプラインが動作

---

### Phase 7: 合成ノード実装
**目標**: CompositeNode の実装

1. `composite_node.h`
2. 統合テスト

**成果物**: 複数入力の合成が動作

---

### Phase 8: タイル分割実装
**目標**: タイル分割評価の実装

1. TileConfig構造体
2. Rendererのタイル処理
3. カスタムRenderer派生クラスのサンプル
4. タイル分割テスト

**成果物**: タイル分割評価が動作

---

### Phase 9: 検証・最適化
**目標**: 動作検証と最適化

1. 既存テストケースの移植・実行
2. パフォーマンス計測基盤
3. 最適化（必要に応じて）

**成果物**: 品質保証されたライブラリ

---

### Phase 10: 統合・置換
**目標**: 既存fleximgとの置換

1. WASMバインディングの改訂
2. デモアプリの更新
3. ドキュメント整備
4. 既存fleximg/の削除
5. fleximg_v2/ → fleximg/ リネーム

**成果物**: 完全移行

---

## リスクと対策

| リスク | 対策 |
|--------|------|
| 設計変更が想定より複雑 | Phase単位で評価、必要に応じて計画修正 |
| 既存機能の再現漏れ | 既存テストケースを移植して確認 |
| パフォーマンス低下 | 計測基盤を早期に導入、ベンチマーク比較 |
| WASMバインディング困難 | Phase 10で対応、最悪は別APIとして維持 |

---

## 進捗

- [x] Phase 1: 基盤構築 (458af1f)
- [x] Phase 2: Port/Node基底 (4065c86)
- [x] Phase 3: SourceNode/SinkNode (fe553fa)
- [x] Phase 4: Renderer基本 (51d6311)
- [x] Phase 5: 処理オペレーション移植
- [x] Phase 6: TransformNode/FilterNode
- [x] Phase 7: CompositeNode
- [x] Phase 8: タイル分割・統合テスト
- [x] Phase 9: 検証テスト追加
- [ ] Phase 10: 統合・置換 ← **次のフェーズ**

---

## 継続用メモ

### 現在のブランチ
```
claude/fleximg-v2-redesign
```

### 作成済みファイル
```
fleximg_v2/
├── src/fleximg_v2/
│   ├── common.h, pixel_format.h, image_allocator.h
│   ├── viewport.h/cpp, image_buffer.h
│   ├── port.h, node.h
│   ├── render_types.h, renderer.h/cpp
│   ├── nodes/
│   │   ├── source_node.h, sink_node.h
│   │   ├── transform_node.h, filter_node.h
│   │   └── composite_node.h
│   └── operations/
│       ├── blend.h/cpp      (first/onto)
│       ├── transform.h/cpp  (affine)
│       └── filters.h/cpp    (brightness/grayscale/boxBlur/alpha)
└── test/
    ├── integration_test.cpp (5テストPASS)
    └── viewport_test.cpp    (61テストPASS)
```

### Phase 10 作業内容
1. WASMバインディング作成 (`bindings_v2.cpp`)
2. デモアプリ更新 (`demo/web/app.js`)
3. 既存fleximg/の削除
4. fleximg_v2/ → fleximg/ リネーム

### 参照元ファイル
- `src/fleximg/bindings.cpp` - 現行WASMバインディング
- `demo/web/app.js` - 現行デモアプリ
