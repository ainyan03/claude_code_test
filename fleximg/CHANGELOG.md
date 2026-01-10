# Changelog

## [2.5.1] - 2026-01-10

### 追加

- **テストパターン画像の拡充**
  - CrossHair (101×63): 奇数×奇数、180度点対称、青枠
  - SmallCheck (70×35): 偶数×奇数、5×5セルのチェック柄

### 変更

- **Checker パターン**: 128×128 → 128×96 に変更（4:3比率）

- **アフィン変換スライダーの精度向上**
  - X/Y移動: step 1 → 0.1
  - 回転: step 1 → 0.1
  - X/Y倍率: step 0.1 → 0.01
  - 行列モード a/b/c/d: step 0.1 → 0.01
  - 行列モード tx/ty: step 1 → 0.1

---

## [2.5.0] - 2026-01-10

### 追加

- **固定小数点型 (types.h)**
  - `int_fixed8` (Q24.8): 座標・origin用の固定小数点型
  - `int_fixed16` (Q16.16): 将来のアフィン行列用固定小数点型
  - 変換関数: `to_fixed8()`, `from_fixed8()`, `float_to_fixed8()` など

### 変更

- **Point 構造体**: float メンバから int_fixed8 メンバに変更
  - マイグレーション用 float コンストラクタを維持
  - `xf()`, `yf()` アクセサで float 値を取得可能

- **RenderRequest/RenderResult**: width/height を int16_t に変更

- **ViewPort/ImageBuffer**:
  - width/height を int16_t に変更
  - stride を int32_t に変更（Y軸反転対応）

- **各ノードの origin 処理を固定小数点化**
  - SourceNode: `setOriginf()` マイグレーション API 追加
  - SinkNode: `setOriginf()` マイグレーション API 追加
  - RendererNode: `setVirtualScreenf()` マイグレーション API 追加
  - blend::first/onto: int_fixed8 引数に変更
  - transform::affine: int_fixed8 引数に変更

### 技術的詳細

- 組み込み環境への移植を見据え、浮動小数点を排除
- Q24.8 形式: 整数部24bit、小数部8bit（精度 1/256 ピクセル）
- クロスプラットフォーム対応のため明示的な型幅を使用

### ファイル追加

- `src/fleximg/types.h`: 固定小数点型定義

---

## [2.4.0] - 2026-01-10

### 変更

- **origin 座標系の統一**: RenderRequest と RenderResult の origin を統一
  - 両方とも「バッファ内での基準点位置」を意味するように変更
  - RenderRequest: `originX/Y` → `Point2f origin` に変更
  - RenderResult: origin の意味を反転（旧: 基準相対座標 → 新: バッファ内位置）
  - 符号反転 (`-origin.x`) が不要になり、コードが明確化

- **影響を受けるノード**:
  - SourceNode: origin 計算を新座標系に変更
  - CompositeNode: blend 引数の符号反転を削除
  - FilterNodeBase/BoxBlurNode: マージン切り出し計算を修正
  - TransformNode: affine 引数の符号反転を削除
  - SinkNode: 配置計算を新座標系に変更
  - RendererNode: RenderRequest 生成を修正

### ドキュメント

- ARCHITECTURE.md: 座標系の説明を更新
- DESIGN_TYPE_STRUCTURE.md: origin の意味を更新

---

## [2.3.1] - 2026-01-10

### ドキュメント

- **DESIGN_TYPE_STRUCTURE.md**: 型構造設計ドキュメントを新規作成
  - ViewPort、ImageBuffer、RenderResult の設計と使用方法
  - コンポジション設計の利点を説明

- **DESIGN_PIXEL_FORMAT.md**: ピクセルフォーマット変換ドキュメントを追加
  - RGBA8_Straight ↔ RGBA16_Premultiplied 変換アルゴリズム
  - アルファ閾値定数の説明

- **GITHUB_PAGES_SETUP.md**: GitHub Pages セットアップガイドを追加
  - 自動ビルド＆デプロイの設定手順
  - トラブルシューティング

- **ドキュメント整合性修正**
  - ARCHITECTURE.md: 関連ドキュメントリンクを更新
  - DESIGN_PERF_METRICS.md: NodeType enum を8種類に更新
  - DESIGN_RENDERER_NODE.md: フィルタノード表記を修正
  - README.md: ファイル構成、ポート番号を更新
  - test/integration_test.cpp: 削除された filter_node.h の参照を修正

---

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
