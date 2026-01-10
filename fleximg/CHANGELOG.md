# Changelog

## [2.3.0] - 2026-01-10

### 追加

- **ImageBuffer::toFormat()**: 効率的なピクセルフォーマット変換メソッド
  - 右辺値参照版（`&&` 修飾）で無駄なコピーを回避
  - 同じフォーマットならムーブ、異なるなら変換

### 変更

- **FilterNode を種類別ノードクラスに分離**
  - `FilterNodeBase`: フィルタ共通基底クラス
  - `BrightnessNode`: 明るさ調整
  - `GrayscaleNode`: グレースケール変換
  - `BoxBlurNode`: ボックスブラー
  - `AlphaNode`: アルファ調整

- **NodeType の個別化**: フィルタ種類別のメトリクス計測が可能に
  - 旧: Source, Filter, Transform, Composite, Output (5種)
  - 新: Source, Transform, Composite, Output, Brightness, Grayscale, BoxBlur, Alpha (8種)

- **WebUI デバッグセクションの動的生成**
  - `NODE_TYPES` 定義による一元管理（将来のノード追加に対応）
  - `NodeTypeHelper` ヘルパー関数
  - フィルタ種類別のメトリクス表示に対応

### 削除

- **旧 FilterNode クラス**: `filter_node.h` を削除
- **FilterType enum**: 種類別ノードに置き換え

### ファイル変更

- `src/fleximg/image_buffer.h`: `toFormat()` メソッド追加
- `src/fleximg/perf_metrics.h`: NodeType enum 拡張
- `src/fleximg/nodes/filter_node_base.h`: 新規追加
- `src/fleximg/nodes/brightness_node.h`: 新規追加
- `src/fleximg/nodes/grayscale_node.h`: 新規追加
- `src/fleximg/nodes/box_blur_node.h`: 新規追加
- `src/fleximg/nodes/alpha_node.h`: 新規追加
- `src/fleximg/nodes/filter_node.h`: 削除

---

## [2.2.0] - 2026-01-10

### 変更

- **RendererNode 導入**: パイプライン実行の発火点をノードとして再設計
  - Renderer クラスを RendererNode に置き換え
  - ノードグラフの一部として統合（`src >> renderer >> sink`）
  - プル/プッシュ型の統一 API（`pullProcess()` / `pushProcess()`）

- **Node API の刷新**
  - `process()`: 共通処理（派生クラスでオーバーライド）
  - `pullProcess()` / `pushProcess()`: プル型/プッシュ型インターフェース
  - `pullPrepare()` / `pushPrepare()`: 準備フェーズの伝播
  - `pullFinalize()` / `pushFinalize()`: 終了フェーズの伝播

### 削除

- **旧 Renderer クラス**: `renderer.h`, `renderer.cpp` を削除
- **旧 API**: `evaluate()`, `UpstreamEvaluator`, `RenderContext` を削除

### ファイル変更

- `src/fleximg/nodes/renderer_node.h`: 新規追加
- `src/fleximg/node.h`: プル/プッシュ API を追加、旧 API を削除
- `src/fleximg/render_types.h`: `RenderContext` を削除

---

## [2.1.0] - 2026-01-09

### 追加

- **パフォーマンス計測基盤**: デバッグビルド時にノード別の処理時間を計測
  - `./build.sh --debug` で有効化
  - ノードタイプ別の処理時間（μs）、呼び出し回数を記録
  - Filter/Transformノードのピクセル効率（wasteRatio）を計測
  - `Renderer::getPerfMetrics()` でC++から取得
  - `evaluator.getPerfMetrics()` でJSから取得

- **メモリ確保統計**: ノード別・グローバルのメモリ使用量を計測
  - `NodeMetrics.allocatedBytes`: ノード別確保バイト数
  - `NodeMetrics.allocCount`: ノード別確保回数
  - `NodeMetrics.maxAllocBytes/Width/Height`: 一回の最大確保サイズ
  - `PerfMetrics.totalAllocatedBytes`: 累計確保バイト数
  - `PerfMetrics.peakMemoryBytes`: ピークメモリ使用量
  - `PerfMetrics.maxAllocBytes/Width/Height`: グローバル最大確保サイズ

- **シングルトンパターン**: `PerfMetrics::instance()` でグローバルアクセス
  - ImageBuffer のコンストラクタ/デストラクタで自動記録
  - 正確なピークメモリ追跡が可能に

### 改善

- **ノード詳細ポップアップ**: UI操作性の向上
  - ヘッダー部分をドラッグしてウィンドウを移動可能に
  - パネル内ボタンクリック時にポップアップが閉じる問題を修正

- **サイドバー状態の保持**: ページ再読み込み時に前回の状態を復元
  - サイドバーの開閉状態を localStorage に保存
  - アコーディオンの展開状態を localStorage に保存

### ファイル追加

- `src/fleximg/perf_metrics.h`: メトリクス構造体定義（シングルトン）
- `docs/DESIGN_PERF_METRICS.md`: 設計ドキュメント

---

## [2.0.0] - 2026-01-09

C++コアライブラリを大幅に刷新しました。

### 主な変更点

- **Node/Port モデル**: ノード間の接続をオブジェクト参照で直接管理
- **Renderer クラス**: パイプライン実行を集中管理
- **ViewPort/ImageBuffer 分離**: メモリ所有と参照の明確な責務分離
- **タイルベースレンダリング**: メモリ効率の良いタイル分割処理

### 機能

- SourceNode: 画像データの提供
- SinkNode: 出力先の管理
- TransformNode: アフィン変換（平行移動、回転、スケール）
- FilterNode: フィルタ処理（明るさ、グレースケール、ぼかし、アルファ）
- CompositeNode: 複数入力の合成

### タイル分割

- 矩形タイル分割
- スキャンライン分割
- デバッグ用チェッカーボードスキップ

### ピクセルフォーマット

- RGBA8_Straight: 入出力用
- RGBA16_Premultiplied: 内部処理用

---

旧バージョン（1.x系）の変更履歴は `fleximg_old/CHANGELOG.md` を参照してください。
